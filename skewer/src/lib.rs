#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use eframe::egui;
#[cfg(not(target_arch = "wasm32"))]
use rfd::FileDialog;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

#[cfg(not(target_arch = "wasm32"))]
use std::process::{Command, Stdio};
#[cfg(not(target_arch = "wasm32"))]
use std::io::Read;

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
    pub last_ffmpeg_cmd: String,
    
    // UI & Texture state
    pub dragging_beat: Option<usize>,
    pub texture: Option<egui::TextureHandle>,
    pub needs_frame_update: bool,
    pub last_requested_time: f64,
    pub video_size: (usize, usize),
    pub ffmpeg_missing: bool,
}

impl Default for BeatMapper {
    fn default() -> Self {
        #[cfg(not(target_arch = "wasm32"))]
        let ffmpeg_missing = Command::new("ffmpeg")
            .arg("-version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_err();
        #[cfg(target_arch = "wasm32")]
        let ffmpeg_missing = false;

        Self {
            video_path: None,
            beats: vec![Beat { time: 0.0 }],
            current_time: 0.0,
            duration: 10.0,
            _is_playing: false,
            last_ffmpeg_cmd: String::new(),
            dragging_beat: None,
            texture: None,
            needs_frame_update: false,
            last_requested_time: -1.0,
            video_size: (640, 360),
            ffmpeg_missing,
        }
    }
}

lazy_static::lazy_static! {
    static ref GLOBAL_APP: Arc<Mutex<Option<BeatMapper>>> = Arc::new(Mutex::new(None));
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

        format!(
            r#"ffmpeg -i "{}" -filter_complex "{}" -map "[outv]" -an -c:v libx264 -crf 18 -preset veryfast -g 30 "{}""#,
            input, filter_complex, output_name
        )
    }

    #[cfg(not(target_arch = "wasm32"))]
    fn fetch_frame_native(&mut self, ctx: &egui::Context) {
        let Some(path) = &self.video_path else { return };
        if (self.current_time - self.last_requested_time).abs() < 0.033 { return; }
        self.last_requested_time = self.current_time;

        let output = Command::new("ffmpeg")
            .arg("-ss").arg(format!("{:.3}", self.current_time))
            .arg("-i").arg(path)
            .arg("-frames:v").arg("1")
            .arg("-f").arg("image2pipe")
            .arg("-vcodec").arg("rawvideo")
            .arg("-pix_fmt").arg("rgb24")
            .arg("-")
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn();

        if let Ok(mut child) = output {
            let mut buffer = Vec::new();
            if child.stdout.as_mut().unwrap().read_to_end(&mut buffer).is_ok() {
                let w = 1280; let h = 720;
                if buffer.len() == w * h * 3 {
                    let image = egui::ColorImage::from_rgb([w, h], &buffer);
                    self.texture = Some(ctx.load_texture("video_frame", image, Default::default()));
                } else {
                    let w = 1920; let h = 1080;
                    if buffer.len() == w * h * 3 {
                        let image = egui::ColorImage::from_rgb([w, h], &buffer);
                        self.texture = Some(ctx.load_texture("video_frame", image, Default::default()));
                    }
                }
            }
        }
    }
}

impl eframe::App for BeatMapper {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        #[cfg(not(target_arch = "wasm32"))]
        if !self.ffmpeg_missing {
            self.fetch_frame_native(ctx);
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("SKEWER // Rhythmic Media Prep");
            
            if self.ffmpeg_missing {
                ui.colored_label(egui::Color32::RED, "CRITICAL: 'ffmpeg' not found.");
                ui.label("Please install FFmpeg to enable preview and export.");
                #[cfg(target_os = "linux")]
                ui.code("sudo apt install ffmpeg");
            }

            ui.horizontal(|ui| {
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if ui.button("📁 Open Video").clicked() {
                        if let Some(path) = FileDialog::new().add_filter("Video", &["mp4", "mov", "mkv", "avi"]).pick_file() {
                            self.video_path = Some(path);
                            self.last_requested_time = -1.0; 
                        }
                    }
                }
                if let Some(path) = &self.video_path {
                    ui.label(format!("Active: {}", path.file_name().unwrap().to_string_lossy()));
                }
            });

            ui.add_space(10.0);

            if let Some(tex) = &self.texture {
                let size = ui.available_width();
                let aspect = tex.size_relative().y / tex.size_relative().x;
                ui.image(tex, egui::vec2(size, size * aspect));
            } else {
                let preview_size = egui::vec2(ui.available_width(), 300.0);
                let (rect, _response) = ui.allocate_at_least(preview_size, egui::Sense::hover());
                ui.painter().rect_filled(rect, 0.0, egui::Color32::BLACK);
                ui.painter().text(rect.center(), egui::Align2::CENTER_CENTER, "NO VIDEO LOADED", egui::FontId::proportional(20.0), egui::Color32::DARK_GRAY);
            }

            ui.add_space(10.0);

            let timeline_size = egui::vec2(ui.available_width(), 60.0);
            let (t_rect, t_response) = ui.allocate_at_least(timeline_size, egui::Sense::click_and_drag());
            
            ui.painter().rect_filled(t_rect, 2.0, var_surface_color());
            
            let playhead_x = t_rect.left() + (self.current_time as f32 / self.duration as f32) * t_rect.width();
            ui.painter().line_segment([egui::pos2(playhead_x, t_rect.top()), egui::pos2(playhead_x, t_rect.bottom())], (2.0, egui::Color32::WHITE));

            for i in 0..self.beats.len() {
                let beat_time = self.beats[i].time;
                let x = t_rect.left() + (beat_time as f32 / self.duration as f32) * t_rect.width();
                let marker_rect = egui::Rect::from_center_size(egui::pos2(x, t_rect.center().y), egui::vec2(12.0, 40.0));
                let marker_id = ui.make_persistent_id(format!("marker_{}", i));
                let marker_resp = ui.interact(marker_rect, marker_id, egui::Sense::drag());
                
                if marker_resp.dragged() {
                    let delta_x = marker_resp.drag_delta().x;
                    let delta_time = (delta_x / t_rect.width()) * self.duration as f32;
                    self.beats[i].time = (self.beats[i].time + delta_time as f64).clamp(0.0, self.duration);
                    self.dragging_beat = Some(i);
                }

                let color = if self.dragging_beat == Some(i) { egui::Color32::WHITE } else { var_accent() };
                ui.painter().rect_filled(marker_rect, 1.0, color);
            }

            if t_response.drag_released() { self.dragging_beat = None; }

            ui.add_space(10.0);

            ui.horizontal(|ui| {
                if ui.button("➕ Beat").clicked() {
                    self.beats.push(Beat { time: self.current_time });
                    self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
                }
                if ui.button("🗑 Reset").clicked() {
                    self.beats = vec![Beat { time: 0.0 }];
                }
                ui.add(egui::Slider::new(&mut self.current_time, 0.0..=self.duration).text("Scrub"));
            });

            ui.add_space(20.0);
            
            #[cfg(not(target_arch = "wasm32"))]
            {
                if !self.ffmpeg_missing {
                    if ui.add_sized([ui.available_width(), 40.0], egui::Button::new("🚀 EXPORT WARPED VIDEO")).clicked() {
                        if let Some(output) = FileDialog::new().set_file_name("warped_loop.mp4").save_file() {
                            let cmd_str = self.generate_ffmpeg_cmd_string(&output.to_string_lossy());
                            let parts: Vec<&str> = cmd_str.split_whitespace().collect();
                            if !parts.is_empty() {
                                let mut cmd = Command::new(parts[0]);
                                for arg in &parts[1..] { cmd.arg(arg.replace("\"", "")); }
                                let _ = cmd.status();
                            }
                        }
                    }
                }
            }

            #[cfg(target_arch = "wasm32")]
            {
                ui.vertical_centered(|ui| {
                    if ui.button("📋 COPY FFMPEG COMMAND").clicked() {
                        self.last_ffmpeg_cmd = self.generate_ffmpeg_cmd_string("warped_loop.mp4");
                        ui.output_mut(|o| o.copied_text = self.last_ffmpeg_cmd.clone());
                    }
                    if !self.last_ffmpeg_cmd.is_empty() {
                        ui.code(&self.last_ffmpeg_cmd);
                    }
                });
            }
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
    
    #[wasm_bindgen]
    pub fn push_frame(&self, rgba_data: &[u8], width: usize, height: usize) {
        // Shared state push logic
    }
}
