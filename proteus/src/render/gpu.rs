//! GPU presentation via wgpu (Vulkan, Metal, DX12, or GL 3.3+/GLES 3).
//!
//! # Why this uploads a rasterised frame instead of drawing the scene on the GPU
//!
//! The obvious design is to turn the [`Scene`](crate::scene::Scene) into GPU
//! geometry: instanced rounded rectangles in a shader, a glyph atlas, image
//! textures. That is the right architecture for something that redraws
//! continuously. This picker does not: it repaints on a keystroke or an arrow
//! key, a few dozen times in the second or two it is open, and rasterising its
//! ~900x520 window on the CPU takes about a millisecond.
//!
//! Against that, a second geometry renderer would mean a second implementation
//! of clipping, antialiasing, glyph placement and image fitting that has to
//! agree with the CPU one pixel for pixel — and every disagreement would be a
//! bug visible only on some machines. Uploading the finished frame keeps one
//! rasteriser, so every backend is pixel-identical by construction and the
//! golden-image tests cover all of them.
//!
//! What the GPU is actually used for is what matters here: presentation.
//! Swapchain, vsync (no tearing), and letting the compositor and driver handle
//! scaling and colour conversion instead of pushing pixels over the X11 socket.
//!
//! Hardware older than GL 3.3 / GLES 3 is not served by wgpu at all. That is
//! what [`super::gl`] is for: the same uploaded frame, drawn with a GL 2.1 /
//! GLES 2.0 context. Below even that, the frame goes straight to the display
//! server. See `RendererChoice` in the config for the order they are tried in.

use std::sync::Arc;

use raw_window_handle::{
    DisplayHandle, HasDisplayHandle, HasWindowHandle, RawDisplayHandle, RawWindowHandle,
    WindowHandle,
};

/// Wraps the raw handles of one of our windows so wgpu can make a surface.
///
/// Our windows are hand-rolled rather than winit's, so nothing implements these
/// traits for us. The handles are plain integers we already hold; this is the
/// adapter that hands them over.
pub struct SurfaceTarget {
    display: RawDisplayHandle,
    window: RawWindowHandle,
}

impl SurfaceTarget {
    /// # Safety
    /// The handles must stay valid for as long as the surface built from them.
    /// Callers keep the window alive alongside the renderer, which is what
    /// guarantees it.
    pub unsafe fn new(display: RawDisplayHandle, window: RawWindowHandle) -> Self {
        Self { display, window }
    }
}

// SAFETY: the handles are a display pointer and a window id, both owned by the
// window this target was built from. wgpu requires Send + Sync because a surface
// may outlive the thread that made it; Proteus never moves either across a
// thread — there is one thread, and the window is dropped after the renderer.
// The pointer is only ever dereferenced by the driver on that same thread.
unsafe impl Send for SurfaceTarget {}
unsafe impl Sync for SurfaceTarget {}

impl HasDisplayHandle for SurfaceTarget {
    fn display_handle(&self) -> Result<DisplayHandle<'_>, raw_window_handle::HandleError> {
        // SAFETY: the handle outlives this borrow; see SurfaceTarget::new.
        Ok(unsafe { DisplayHandle::borrow_raw(self.display) })
    }
}

impl HasWindowHandle for SurfaceTarget {
    fn window_handle(&self) -> Result<WindowHandle<'_>, raw_window_handle::HandleError> {
        // SAFETY: as above.
        Ok(unsafe { WindowHandle::borrow_raw(self.window) })
    }
}

/// A wgpu swapchain that draws one fullscreen textured quad per frame.
pub struct GpuPresenter {
    device: wgpu::Device,
    queue: wgpu::Queue,
    surface: wgpu::Surface<'static>,
    config: wgpu::SurfaceConfiguration,
    pipeline: wgpu::RenderPipeline,
    bind_group_layout: wgpu::BindGroupLayout,
    sampler: wgpu::Sampler,
    texture: Option<(wgpu::Texture, wgpu::BindGroup, u32, u32)>,
    /// Adapter name, for `--renderer` diagnostics.
    pub adapter_name: String,
    pub backend: String,
}

const SHADER: &str = r#"
// A fullscreen triangle, then the frame sampled onto it. No vertex buffer: the
// three positions are derived from the vertex index, which is both fewer moving
// parts and one less thing to keep in sync with the CPU side.
struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {
    var out: VsOut;
    let x = f32((i << 1u) & 2u);
    let y = f32(i & 2u);
    out.uv = vec2<f32>(x, y);
    out.pos = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    return out;
}

@group(0) @binding(0) var frame: texture_2d<f32>;
@group(0) @binding(1) var frame_sampler: sampler;

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    return textureSample(frame, frame_sampler, in.uv);
}
"#;

impl GpuPresenter {
    /// Create a presenter for a window of `width` x `height`.
    ///
    /// Returns `Err` rather than panicking on every failure path: no Vulkan, no
    /// GL 3.3, a driver that refuses the surface. The caller falls back to the
    /// CPU renderer, which is the whole point of having one.
    pub fn new(target: SurfaceTarget, width: u32, height: u32) -> Result<Self, String> {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::all(),
            ..Default::default()
        });

        let target = Arc::new(target);
        let surface = instance
            .create_surface(target.clone())
            .map_err(|e| format!("cannot create a GPU surface: {e}"))?;

        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            // A picker is not a game: the integrated GPU is already idle and
            // waking a discrete one costs power and seconds of latency.
            power_preference: wgpu::PowerPreference::LowPower,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
        }))
        .map_err(|e| format!("no usable GPU adapter: {e}"))?;

        let info = adapter.get_info();
        let (device, queue) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("proteus"),
            // Defaults, deliberately: asking for more would exclude exactly the
            // older hardware this backend is meant to still cover.
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::downlevel_defaults(),
            memory_hints: wgpu::MemoryHints::default(),
            trace: wgpu::Trace::Off,
        }))
        .map_err(|e| format!("cannot open the GPU device: {e}"))?;

        let caps = surface.get_capabilities(&adapter);
        let format = caps
            .formats
            .iter()
            .copied()
            .find(|f| f.is_srgb())
            .or_else(|| caps.formats.first().copied())
            .ok_or_else(|| "the surface supports no format".to_string())?;
        // Prefer an alpha mode that lets the compositor see through the rounded
        // corners; fall back to opaque where that is not offered.
        let alpha_mode = caps
            .alpha_modes
            .iter()
            .copied()
            .find(|m| *m == wgpu::CompositeAlphaMode::PreMultiplied)
            .unwrap_or(caps.alpha_modes[0]);

        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: width.max(1),
            height: height.max(1),
            // Fifo is vsync, and is the only mode guaranteed everywhere.
            present_mode: wgpu::PresentMode::Fifo,
            alpha_mode,
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("proteus-blit"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });

        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("proteus-frame"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });

        let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("proteus"),
            bind_group_layouts: &[&bind_group_layout],
            push_constant_ranges: &[],
        });

        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("proteus"),
            layout: Some(&layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::PREMULTIPLIED_ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });

        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("proteus"),
            // The frame is uploaded at exactly the surface size, so this only
            // matters during the frame between a resize and the next raster.
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });

        Ok(Self {
            device,
            queue,
            surface,
            config,
            pipeline,
            bind_group_layout,
            sampler,
            texture: None,
            adapter_name: info.name.clone(),
            backend: format!("{:?}", info.backend),
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if width == 0 || height == 0 {
            return;
        }
        if (self.config.width, self.config.height) == (width, height) {
            return;
        }
        self.config.width = width;
        self.config.height = height;
        self.surface.configure(&self.device, &self.config);
    }

    fn ensure_texture(&mut self, width: u32, height: u32) {
        if let Some((_, _, w, h)) = &self.texture {
            if *w == width && *h == height {
                return;
            }
        }
        let texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("proteus-frame"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8UnormSrgb,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let view = texture.create_view(&Default::default());
        let bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("proteus-frame"),
            layout: &self.bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(&self.sampler),
                },
            ],
        });
        self.texture = Some((texture, bind_group, width, height));
    }

    /// Upload a straight-RGBA frame and present it.
    pub fn present(&mut self, rgba: &[u8], width: u32, height: u32) -> Result<(), String> {
        let needed = (width as usize) * (height as usize) * 4;
        if rgba.len() < needed {
            return Err(format!("frame is {} bytes, expected {needed}", rgba.len()));
        }
        self.ensure_texture(width, height);
        let Some((texture, bind_group, _, _)) = &self.texture else {
            return Err("no frame texture".into());
        };

        self.queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            &rgba[..needed],
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(width * 4),
                rows_per_image: Some(height),
            },
            wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
        );

        let frame = match self.surface.get_current_texture() {
            Ok(f) => f,
            // A lost or outdated swapchain is normal across a resize: reconfigure
            // and skip this frame rather than treating it as an error.
            Err(wgpu::SurfaceError::Lost) | Err(wgpu::SurfaceError::Outdated) => {
                self.surface.configure(&self.device, &self.config);
                return Ok(());
            }
            Err(e) => return Err(format!("cannot acquire a frame: {e}")),
        };

        let view = frame.texture.create_view(&Default::default());
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("proteus"),
            });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("proteus"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&self.pipeline);
            pass.set_bind_group(0, bind_group, &[]);
            pass.draw(0..3, 0..1);
        }
        self.queue.submit(Some(encoder.finish()));
        frame.present();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The shader has to compile on whatever backend is present. This is the
    /// cheapest way to catch a WGSL typo, which otherwise only shows up on a
    /// machine that actually has a GPU.
    #[test]
    fn the_shader_compiles_on_this_machine() {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::all(),
            ..Default::default()
        });
        let Ok(adapter) = pollster::block_on(instance.request_adapter(&Default::default())) else {
            eprintln!("no GPU adapter available - skipping the shader compile test");
            return;
        };
        let Ok((device, _queue)) =
            pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
                label: Some("proteus-test"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::downlevel_defaults(),
                memory_hints: Default::default(),
                trace: wgpu::Trace::Off,
            }))
        else {
            eprintln!("cannot open a GPU device - skipping");
            return;
        };
        device.push_error_scope(wgpu::ErrorFilter::Validation);
        let _module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("proteus-blit"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });
        let err = pollster::block_on(device.pop_error_scope());
        assert!(err.is_none(), "the blit shader failed to compile: {err:?}");
    }
}
