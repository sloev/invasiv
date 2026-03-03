#[cfg(not(target_arch = "wasm32"))]
fn main() -> eframe::Result<()> {
    env_logger::init_from_env(env_logger::Env::default().default_filter_or("info"));
    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default().with_inner_size([800.0, 600.0]),
        ..Default::default()
    };
    eframe::run_native(
        "SKEWER",
        options,
        Box::new(|cc| Box::new(skewer::BeatMapper::new(cc))),
    )
}

#[cfg(target_arch = "wasm32")]
fn main() {}
// CI_FORCE_REBUILD
