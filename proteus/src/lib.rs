//! Proteus — a theme and wallpaper picker that runs on X11 and Wayland.
//!
//! Named for the shape-shifting sea god: it changes what everything looks like.
//!
//! The crate is split so that everything decidable without a display server is
//! decided without one. `catalog`, `fuzzy` and `model` are pure and unit-tested;
//! the rendering and windowing layers sit on top and are the only parts that
//! need a compositor.

pub mod anim;
pub mod app;
pub mod catalog;
pub mod config;
pub mod fuzzy;
pub mod images;
pub mod model;
pub mod platform;
pub mod render;
pub mod run;
pub mod scene;
pub mod text;
pub mod ui;
