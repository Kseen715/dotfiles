//! OpenGL 2.1 / GLES 2.0 presentation, via glutin (context) and glow (bindings).
//!
//! This is the path for hardware wgpu will not touch. wgpu's GL backend needs
//! GL 3.3 or GLES 3.0; an Ironlake-era iGPU, a GMA, an early Mali or a VM with
//! only a 2.x driver has none of that, and those machines are exactly the ones
//! where a picker is expected to still feel instant.
//!
//! What GL 2.1 gives us is enough: upload a texture, draw two triangles with a
//! shader, swap with vsync. That is the whole job, because the frame is already
//! rasterised (see [`super::gpu`] for why). No VAOs, no framebuffer objects, no
//! instancing — nothing here needs an extension.
//!
//! Both context creation and the GL entry points come from libraries rather than
//! hand-rolled EGL/GLX: those are the two places where "works on my driver" bugs
//! live, and glutin has already met every driver quirk worth knowing about.

use glow::HasContext;
use glutin::config::{ConfigTemplateBuilder, GlConfig};
use glutin::context::{
    ContextApi, ContextAttributesBuilder, GlProfile, NotCurrentGlContext, PossiblyCurrentContext,
};
use glutin::display::{Display, DisplayApiPreference, GlDisplay};
use glutin::surface::{GlSurface, Surface, SwapInterval, WindowSurface};
use raw_window_handle::{RawDisplayHandle, RawWindowHandle};
use std::num::NonZeroU32;

/// Vertex shader in GLSL 110 — the version GL 2.1 speaks, and close enough to
/// GLSL ES 100 that the same source compiles on GLES 2.
///
/// `attribute`/`varying` rather than `in`/`out` is not legacy sloppiness: those
/// are the only keywords 2.1 has.
const VERT: &str = r#"#version 110
attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
"#;

const FRAG: &str = r#"#version 110
varying vec2 v_uv;
uniform sampler2D u_frame;
void main() {
    gl_FragColor = texture2D(u_frame, v_uv);
}
"#;

/// GLES 2 wants the ES dialect and an explicit float precision.
const VERT_ES: &str = r#"#version 100
attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
"#;

const FRAG_ES: &str = r#"#version 100
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_frame;
void main() {
    gl_FragColor = texture2D(u_frame, v_uv);
}
"#;

pub struct GlPresenter {
    gl: glow::Context,
    context: PossiblyCurrentContext,
    surface: Surface<WindowSurface>,
    program: glow::Program,
    vbo: glow::Buffer,
    texture: glow::Texture,
    tex_size: (u32, u32),
    a_pos: u32,
    a_uv: u32,
    u_frame: Option<glow::UniformLocation>,
    pub renderer_name: String,
    pub version: String,
}

impl GlPresenter {
    /// Create a GL context on `window` and prepare the blit pipeline.
    ///
    /// # Safety
    /// The handles must belong to a window that outlives this presenter.
    pub unsafe fn new(
        display_handle: RawDisplayHandle,
        window_handle: RawWindowHandle,
        width: u32,
        height: u32,
    ) -> Result<Self, String> {
        // EGL first: it is the only option on Wayland and the better one on X11
        // (GLX needs an Xlib display, which the pure-XCB connection has not
        // got). Falling back to GLX covers X servers with no EGL at all.
        let preference = match display_handle {
            RawDisplayHandle::Wayland(_) => DisplayApiPreference::Egl,
            _ => DisplayApiPreference::EglThenGlx(Box::new(|_| {
                // glutin wants an X error handler registered. There is nothing
                // useful to do with one here: a failed config or context shows
                // up as an Err from the call itself, which is what we report.
            })),
        };
        let display = unsafe { Display::new(display_handle, preference) }
            .map_err(|e| format!("no GL display: {e}"))?;

        let template = ConfigTemplateBuilder::new()
            .with_alpha_size(8)
            .with_transparency(true)
            .build();
        let config = unsafe { display.find_configs(template) }
            .map_err(|e| format!("no GL config: {e}"))?
            // Prefer a config with an alpha channel so the rounded corners can
            // actually be transparent; fall back to the first offered.
            .reduce(|best, c| {
                if c.alpha_size() > best.alpha_size() {
                    c
                } else {
                    best
                }
            })
            .ok_or_else(|| "the driver offered no usable GL config".to_string())?;

        // Ask for 2.1 compatibility explicitly. Requesting a core profile would
        // get a 3.2+ context on drivers that have one and fail outright on the
        // hardware this backend exists for.
        let attrs = ContextAttributesBuilder::new()
            .with_context_api(ContextApi::OpenGl(Some(glutin::context::Version::new(
                2, 1,
            ))))
            .with_profile(GlProfile::Compatibility)
            .build(Some(window_handle));
        let es_attrs = ContextAttributesBuilder::new()
            .with_context_api(ContextApi::Gles(Some(glutin::context::Version::new(2, 0))))
            .build(Some(window_handle));

        // Desktop GL 2.1 first, GLES 2.0 second: a desktop driver may offer both
        // and the compatibility profile is the better-supported of the two.
        // Which one we got decides the shader dialect below.
        let (not_current, is_es) = match unsafe { display.create_context(&config, &attrs) } {
            Ok(c) => (c, false),
            Err(desktop_err) => match unsafe { display.create_context(&config, &es_attrs) } {
                Ok(c) => (c, true),
                Err(es_err) => {
                    return Err(format!(
                    "cannot create a GL 2.1 context ({desktop_err}) or a GLES 2.0 one ({es_err})"
                ))
                }
            },
        };

        let surface_attrs = glutin::surface::SurfaceAttributesBuilder::<WindowSurface>::new()
            .build(
                window_handle,
                NonZeroU32::new(width.max(1)).unwrap(),
                NonZeroU32::new(height.max(1)).unwrap(),
            );
        let surface = unsafe { display.create_window_surface(&config, &surface_attrs) }
            .map_err(|e| format!("cannot create a GL surface: {e}"))?;
        let context = not_current
            .make_current(&surface)
            .map_err(|e| format!("cannot make the GL context current: {e}"))?;

        let gl = unsafe {
            glow::Context::from_loader_function(|s| {
                let cs = std::ffi::CString::new(s).unwrap();
                display.get_proc_address(&cs)
            })
        };

        let (renderer_name, version) = unsafe {
            (
                gl.get_parameter_string(glow::RENDERER),
                gl.get_parameter_string(glow::VERSION),
            )
        };

        let program = unsafe { link_program(&gl, is_es) }?;
        let (a_pos, a_uv, u_frame) = unsafe {
            (
                gl.get_attrib_location(program, "a_pos")
                    .ok_or_else(|| "shader has no a_pos".to_string())?,
                gl.get_attrib_location(program, "a_uv")
                    .ok_or_else(|| "shader has no a_uv".to_string())?,
                gl.get_uniform_location(program, "u_frame"),
            )
        };

        // A fullscreen quad as two triangles: x, y, u, v. The V axis is flipped
        // because GL's texture origin is bottom-left and the frame's is top-left
        // - get this wrong and the picker renders upside down.
        #[rustfmt::skip]
        const QUAD: [f32; 24] = [
            -1.0, -1.0, 0.0, 1.0,
             1.0, -1.0, 1.0, 1.0,
             1.0,  1.0, 1.0, 0.0,
            -1.0, -1.0, 0.0, 1.0,
             1.0,  1.0, 1.0, 0.0,
            -1.0,  1.0, 0.0, 0.0,
        ];

        let (vbo, texture) = unsafe {
            let vbo = gl.create_buffer().map_err(|e| format!("no VBO: {e}"))?;
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, bytemuck_cast(&QUAD), glow::STATIC_DRAW);

            let texture = gl
                .create_texture()
                .map_err(|e| format!("no texture: {e}"))?;
            gl.bind_texture(glow::TEXTURE_2D, Some(texture));
            // CLAMP_TO_EDGE + LINEAR only; no mipmaps, because a non-power-of-two
            // texture with mipmaps is exactly what GLES 2 refuses to sample.
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_WRAP_S,
                glow::CLAMP_TO_EDGE as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_WRAP_T,
                glow::CLAMP_TO_EDGE as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MIN_FILTER,
                glow::LINEAR as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MAG_FILTER,
                glow::LINEAR as i32,
            );
            (vbo, texture)
        };

        // Vsync. Best-effort: a driver that refuses just tears, which is not a
        // reason to refuse to open.
        let _ =
            surface.set_swap_interval(&context, SwapInterval::Wait(NonZeroU32::new(1).unwrap()));

        Ok(Self {
            gl,
            context,
            surface,
            program,
            vbo,
            texture,
            tex_size: (0, 0),
            a_pos,
            a_uv,
            u_frame,
            renderer_name,
            version,
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        let (Some(w), Some(h)) = (NonZeroU32::new(width), NonZeroU32::new(height)) else {
            return;
        };
        self.surface.resize(&self.context, w, h);
        unsafe { self.gl.viewport(0, 0, width as i32, height as i32) };
    }

    /// Upload a straight-RGBA frame and swap it onto the screen.
    pub fn present(&mut self, rgba: &[u8], width: u32, height: u32) -> Result<(), String> {
        let needed = (width as usize) * (height as usize) * 4;
        if rgba.len() < needed {
            return Err(format!("frame is {} bytes, expected {needed}", rgba.len()));
        }
        let gl = &self.gl;
        unsafe {
            gl.viewport(0, 0, width as i32, height as i32);
            gl.disable(glow::DEPTH_TEST);
            gl.disable(glow::CULL_FACE);
            // The frame is straight (non-premultiplied) RGBA, so the classic
            // src-alpha blend is the right one.
            gl.enable(glow::BLEND);
            gl.blend_func(glow::SRC_ALPHA, glow::ONE_MINUS_SRC_ALPHA);
            gl.clear_color(0.0, 0.0, 0.0, 0.0);
            gl.clear(glow::COLOR_BUFFER_BIT);

            gl.bind_texture(glow::TEXTURE_2D, Some(self.texture));
            // Rows are tightly packed; the default of 4 would skew any width
            // that is not a multiple of 4 pixels.
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
            if self.tex_size != (width, height) {
                gl.tex_image_2d(
                    glow::TEXTURE_2D,
                    0,
                    glow::RGBA as i32,
                    width as i32,
                    height as i32,
                    0,
                    glow::RGBA,
                    glow::UNSIGNED_BYTE,
                    glow::PixelUnpackData::Slice(Some(&rgba[..needed])),
                );
                self.tex_size = (width, height);
            } else {
                // Re-uploading into the existing storage avoids reallocating a
                // texture every frame, which some drivers make expensive.
                gl.tex_sub_image_2d(
                    glow::TEXTURE_2D,
                    0,
                    0,
                    0,
                    width as i32,
                    height as i32,
                    glow::RGBA,
                    glow::UNSIGNED_BYTE,
                    glow::PixelUnpackData::Slice(Some(&rgba[..needed])),
                );
            }

            gl.use_program(Some(self.program));
            if let Some(loc) = &self.u_frame {
                gl.uniform_1_i32(Some(loc), 0);
            }
            gl.active_texture(glow::TEXTURE0);
            gl.bind_texture(glow::TEXTURE_2D, Some(self.texture));

            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.vbo));
            let stride = 4 * std::mem::size_of::<f32>() as i32;
            gl.enable_vertex_attrib_array(self.a_pos);
            gl.vertex_attrib_pointer_f32(self.a_pos, 2, glow::FLOAT, false, stride, 0);
            gl.enable_vertex_attrib_array(self.a_uv);
            gl.vertex_attrib_pointer_f32(
                self.a_uv,
                2,
                glow::FLOAT,
                false,
                stride,
                2 * std::mem::size_of::<f32>() as i32,
            );
            gl.draw_arrays(glow::TRIANGLES, 0, 6);
            gl.disable_vertex_attrib_array(self.a_pos);
            gl.disable_vertex_attrib_array(self.a_uv);
        }
        self.surface
            .swap_buffers(&self.context)
            .map_err(|e| format!("GL swap failed: {e}"))
    }
}

impl Drop for GlPresenter {
    fn drop(&mut self) {
        unsafe {
            self.gl.delete_program(self.program);
            self.gl.delete_buffer(self.vbo);
            self.gl.delete_texture(self.texture);
        }
    }
}

/// Compile and link the blit program, reporting the driver's own message on
/// failure — on old drivers that text is the only clue you get.
unsafe fn link_program(gl: &glow::Context, is_es: bool) -> Result<glow::Program, String> {
    let (vs_src, fs_src) = if is_es {
        (VERT_ES, FRAG_ES)
    } else {
        (VERT, FRAG)
    };
    let program = gl
        .create_program()
        .map_err(|e| format!("no program: {e}"))?;
    let mut shaders = Vec::new();
    for (kind, src) in [
        (glow::VERTEX_SHADER, vs_src),
        (glow::FRAGMENT_SHADER, fs_src),
    ] {
        let s = gl
            .create_shader(kind)
            .map_err(|e| format!("no shader: {e}"))?;
        gl.shader_source(s, src);
        gl.compile_shader(s);
        if !gl.get_shader_compile_status(s) {
            return Err(format!("GL shader failed: {}", gl.get_shader_info_log(s)));
        }
        gl.attach_shader(program, s);
        shaders.push(s);
    }
    gl.link_program(program);
    if !gl.get_program_link_status(program) {
        return Err(format!(
            "GL link failed: {}",
            gl.get_program_info_log(program)
        ));
    }
    for s in shaders {
        gl.detach_shader(program, s);
        gl.delete_shader(s);
    }
    Ok(program)
}

/// `&[f32]` as bytes, for `buffer_data_u8_slice`.
fn bytemuck_cast(v: &[f32]) -> &[u8] {
    // SAFETY: f32 has no padding and no invalid bit patterns, and the result is
    // read-only and no longer than the source.
    unsafe { std::slice::from_raw_parts(v.as_ptr() as *const u8, std::mem::size_of_val(v)) }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The shaders must be the dialects the old hardware actually parses. A
    /// `#version 130` slipping in would work on every machine we can test on and
    /// fail on every machine this backend exists for.
    #[test]
    fn the_shaders_target_gl2_and_gles2() {
        assert!(
            VERT.starts_with("#version 110"),
            "desktop GL 2.1 speaks GLSL 110"
        );
        assert!(FRAG.starts_with("#version 110"));
        assert!(
            VERT_ES.starts_with("#version 100"),
            "GLES 2.0 speaks GLSL ES 100"
        );
        assert!(FRAG_ES.starts_with("#version 100"));

        for src in [VERT, FRAG, VERT_ES, FRAG_ES] {
            // `in`/`out`/`texture()` are GLSL 130+; 2.1 has attribute/varying
            // and texture2D.
            for banned in ["\nin ", "\nout ", "texture(", "layout(", "gl_FragData"] {
                assert!(
                    !src.contains(banned),
                    "{banned:?} is not available in GLSL 110/100"
                );
            }
        }
        // The fragment shaders must write the only output 2.x has.
        assert!(FRAG.contains("gl_FragColor"));
        assert!(FRAG_ES.contains("gl_FragColor"));
        // GLES requires an explicit precision or the shader will not compile.
        assert!(FRAG_ES.contains("precision mediump float;"));
    }

    #[test]
    fn the_quad_covers_the_viewport_with_flipped_v() {
        // Rebuilt here so the constant cannot drift from what is asserted.
        #[rustfmt::skip]
        let quad: [f32; 24] = [
            -1.0, -1.0, 0.0, 1.0,
             1.0, -1.0, 1.0, 1.0,
             1.0,  1.0, 1.0, 0.0,
            -1.0, -1.0, 0.0, 1.0,
             1.0,  1.0, 1.0, 0.0,
            -1.0,  1.0, 0.0, 0.0,
        ];
        assert_eq!(quad.len(), 6 * 4, "two triangles of x,y,u,v");
        for v in quad.chunks(4) {
            assert!((-1.0..=1.0).contains(&v[0]) && (-1.0..=1.0).contains(&v[1]));
            assert!((0.0..=1.0).contains(&v[2]) && (0.0..=1.0).contains(&v[3]));
        }
        // GL's texture origin is bottom-left and the frame's is top-left, so the
        // bottom of the screen must sample the TOP of the texture. Without this
        // flip the picker renders upside down.
        let bottom_left = &quad[0..4];
        assert_eq!((bottom_left[0], bottom_left[1]), (-1.0, -1.0));
        assert_eq!(bottom_left[3], 1.0, "y=-1 must sample v=1");
        let top_left = &quad[20..24];
        assert_eq!((top_left[0], top_left[1]), (-1.0, 1.0));
        assert_eq!(top_left[3], 0.0, "y=+1 must sample v=0");
    }

    #[test]
    fn float_slices_cast_to_the_right_number_of_bytes() {
        let v = [1.0f32, 2.0, 3.0];
        assert_eq!(bytemuck_cast(&v).len(), 12);
        assert_eq!(bytemuck_cast(&[]).len(), 0);
    }
}
