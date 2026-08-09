//! Renderers. Each consumes a `Scene` and puts pixels somewhere.
//!
//! The CPU rasteriser is the reference: it defines what correct output looks
//! like, and the GPU backends are checked against images it produces.

pub mod cpu;
pub mod gl;
pub mod gpu;
