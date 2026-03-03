#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use eframe::egui;
#[cfg(not(target_arch = "wasm32"))]
use rfd::FileDialog;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[cfg(not(target_arch = "wasm32"))]
use std::process::Command;

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Beat {
    pub time: f64, 
}

pub struct BeatMapper {
    pub video_path: Option<PathBuf>,
    pub beats: Vec<Beat>,
    pub current_time: f64,
    pub duration: f64,
    pub _is_playing: bool,
    pub exporting: bool,
    pub export_progress: f32,
    pub last_ffmpeg_cmd: String,
}

impl Default for BeatMapper {
    fn default() -> Self {
        Self {
            video_path: None,
            beats: vec![Beat { time: 0.0 }],
            current_time: 0.0,
            duration: 10.0,
            _is_playing: false,
            exporting: false,
            export_progress: 0.0,
            last_ffmpeg_cmd: String::new(),
        }
    }
}

impl BeatMapper {
    pub fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        #[cfg(target_arch = "wasm32")]
        _cc.egui_ctx.request_repaint();
        Self::default()
    }

    pub fn generate_ffmpeg_cmd_string(&self, output_name: &str) -> String {
        let input = self.video_path.as_ref()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|| "input.mp4".to_string());
        
        let mut filter_complex = String::new();
        let mut concat_parts = String::new();
        
        for (i, pair) in self.beats.windows(2).enumerate() {
            let start = pair[0].time;
            let end = pair[1].time;
            let segment_duration = end - start;
            if segment_duration <= 0.0 { continue; }
            let scale = 2.0 / segment_duration;
            
            filter_complex.push_str(&format!(
                "[0:v]trim=start={}:end={},setpts={:.6}*(PTS-STARTPTS)[v{}];",
                start, end, 1.0 / scale, i
            ));
            concat_parts.push_str(&format!("[v{}]", i));
        }
        
        if self.beats.len() > 1 {
            filter_complex.push_str(&format!(
                "{}concat=n={}:v=1:a=0[outv]",
                concat_parts,
                self.beats.len() - 1
            ));
        }

        // Use raw string to avoid escaping hell
        format!(
            r#"ffmpeg -i "{}" -filter_complex "{}" -map "[outv]" -an -c:v libx264 -crf 18 -preset veryfast -g 30 "{}""#,
            input, filter_complex, output_name
        )
    }
}

impl eframe::App for BeatMapper {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("SKEWER // Rhythmic Warper");
            ui.add_space(10.0);

            ui.horizontal(|ui| {
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if ui.button("📁 Open Video").clicked() {
                        let task = FileDialog::new()
                            .add_filter("Video", &["mp4", "mov", "mkv", "avi"])
                            .pick_file();
                        if let Some(path) = task {
                            self.video_path = Some(path);
                        }
                    }
                }
                #[cfg(target_arch = "wasm32")]
                {
                    ui.label("📁 [Web: Use native for file loading]");
                }
                if let Some(path) = &self.video_path {
                    ui.label(format!("Active: {}", path.file_name().unwrap().to_string_lossy()));
                }
            });

            ui.add_space(20.0);

            let (rect, _response) = ui.allocate_at_least(
                egui::vec2(ui.available_width(), 100.0),
                egui::Sense::click_and_drag()
            );
            
            ui.painter().rect_filled(rect, 2.0, var_surface_color());
            
            for sec in 0..=(self.duration as i32) {
                let x = rect.left() + (sec as f32 / self.duration as f32) * rect.width();
                ui.painter().line_segment(
                    [egui::pos2(x, rect.top()), egui::pos2(x, rect.top() + 10.0)],
                    (1.0, var_text_dim())
                );
            }

            for i in 0..self.beats.len() {
                let beat_time = self.beats[i].time;
                let x = rect.left() + (beat_time as f32 / self.duration as f32) * rect.width();
                let beat_rect = egui::Rect::from_center_size(egui::pos2(x, rect.center().y), egui::vec2(10.0, 40.0));
                let beat_id = ui.make_persistent_id(format!("beat_{}", i));
                let beat_resp = ui.interact(beat_rect, beat_id, egui::Sense::drag());
                
                if beat_resp.dragged() {
                    let delta_x = beat_resp.drag_delta().x;
                    let delta_time = (delta_x / rect.width()) * self.duration as f32;
                    self.beats[i].time = (self.beats[i].time + delta_time as f64).clamp(0.0, self.duration);
                }

                ui.painter().rect_filled(beat_rect, 2.0, var_accent());
                ui.painter().text(beat_rect.left_top(), egui::Align2::LEFT_BOTTOM, format!("{:.2}s", beat_time), egui::FontId::monospace(10.0), egui::Color32::WHITE);
            }

            ui.add_space(20.0);

            ui.horizontal(|ui| {
                if ui.button("➕ Add Beat").clicked() {
                    self.beats.push(Beat { time: self.current_time });
                    self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
                }
                if ui.button("🗑 Reset").clicked() {
                    self.beats = vec![Beat { time: 0.0 }];
                }
            });

            ui.add_space(40.0);
            
            #[cfg(not(target_arch = "wasm32"))]
            {
                if ui.add_sized([ui.available_width(), 40.0], egui::Button::new("🚀 EXPORT WARPED VIDEO (FFMPEG)")).clicked() {
                    if let Some(output) = FileDialog::new().set_file_name("warped_loop.mp4").save_file() {
                        let cmd_str = self.generate_ffmpeg_cmd_string(&output.to_string_lossy());
                        println!("Executing: {}", cmd_str);
                        let parts: Vec<&str> = cmd_str.split_whitespace().collect();
                        if !parts.is_empty() {
                            let mut cmd = Command::new(parts[0]);
                            for arg in &parts[1..] { cmd.arg(arg.replace("\"", "")); }
                            match cmd.status() {
                                Ok(s) => println!("Success: {}", s),
                                Err(e) => println!("Error: {}", e),
                            }
                        }
                    }
                }
            }

            #[cfg(target_arch = "wasm32")]
            {
                ui.vertical_centered(|ui| {
                    ui.label("Web Version: re-encoding requires local FFmpeg.");
                    if ui.button("📋 COPY FFMPEG COMMAND").clicked() {
                        self.last_ffmpeg_cmd = self.generate_ffmpeg_cmd_string("warped_loop.mp4");
                        ui.output_mut(|o| o.copied_text = self.last_ffmpeg_cmd.clone());
                    }
                    if !self.last_ffmpeg_cmd.is_empty() {
                        ui.add_space(10.0);
                        ui.code(&self.last_ffmpeg_cmd);
                    }
                });
            }
            
            ui.with_layout(egui::Layout::bottom_up(egui::Align::Center), |ui| {
                ui.label("PRO TIP: Every beat interval will become exactly 2.0 seconds in the output.");
            });
        });
    }
}

fn var_surface_color() -> egui::Color32 { egui::Color32::from_rgb(26, 26, 26) }
fn var_text_dim() -> egui::Color32 { egui::Color32::from_rgb(102, 102, 102) }
fn var_accent() -> egui::Color32 { egui::Color32::from_rgb(255, 59, 48) }

// --- WASM ENTRY POINT ---
#[cfg(target_arch = "wasm32")]
use wasm_bindgen::prelude::*;

#[cfg(target_arch = "wasm32")]
#[wasm_bindgen]
pub struct WebHandle {
    runner: eframe::WebRunner,
}

#[cfg(target_arch = "wasm32")]
#[wasm_bindgen]
impl WebHandle {
    #[wasm_bindgen(constructor)]
    pub fn new() -> Self {
        Self { runner: eframe::WebRunner::new() }
    }

    #[wasm_bindgen]
    pub async fn start(&self, canvas_id: &str) -> Result<(), JsValue> {
        self.runner.start(
            canvas_id,
            eframe::WebOptions::default(),
            Box::new(|cc| Box::new(BeatMapper::new(cc))),
        ).await
    }
}
