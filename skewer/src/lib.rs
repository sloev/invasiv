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
    pub weight: u32, // Number of 0.5s beats this segment lasts
}

#[derive(Default)]
pub struct AppState {
    pub video_path: Option<PathBuf>,
    pub load_video_clicked: bool,
    pub export_clicked: bool,
    pub last_generated_cmd: String,
    pub frame_data: Option<(Vec<u8>, (usize, usize))>,
    pub current_time: f64,
    pub duration: f64,
    pub is_preview: bool,
}

lazy_static::lazy_static! {
    static ref SHARED_STATE: Arc<Mutex<AppState>> = Arc::new(Mutex::new(AppState::default()));
}

pub struct BeatMapper {
    pub video_path: Option<PathBuf>,
    pub beats: Vec<Beat>,
    pub current_time: f64,
    pub steady_time: f64, // Virtual "musical" time
    pub duration: f64,
    pub trim_start: f64,
    pub trim_end: f64,
    pub is_playing: bool,
    pub is_preview: bool,
    pub last_ffmpeg_cmd: String,
    
    // UI & Texture state
    pub dragging_beat: Option<usize>,
    pub selected_segment: Option<usize>,
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
            beats: vec![Beat { time: 0.0, weight: 1 }],
            current_time: 0.0,
            steady_time: 0.0,
            duration: 10.0,
            trim_start: 0.0,
            trim_end: 10.0,
            is_playing: false,
            is_preview: false,
            last_ffmpeg_cmd: String::new(),
            dragging_beat: None,
            selected_segment: None,
            texture: None,
            needs_frame_update: false,
            last_requested_time: -1.0,
            video_size: (640, 360),
            ffmpeg_missing,
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
        
        // Filter beats within trim range
        let mut active_beats = self.beats.clone();
        active_beats.retain(|b| b.time >= self.trim_start && b.time <= self.trim_end);
        
        let segments = active_beats.len().saturating_sub(1);
        
        for (i, pair) in active_beats.windows(2).enumerate() {
            let start = pair[0].time;
            let end = pair[1].time;
            let weight = pair[0].weight as f64;
            let segment_duration = end - start;
            if segment_duration <= 0.001 { continue; }
            
            // Target each segment to be exactly (weight * 0.5)s
            let target_dur = weight * 0.5;
            let multiplier = target_dur / segment_duration;
            
            let out_tag = if segments == 1 { "outv".to_string() } else { format!("v{}", i) };
            
            filter_complex.push_str(&format!(
                "[0:v]trim=start={}:end={},setpts={:.6}*(PTS-STARTPTS)[{}];",
                start, end, multiplier, out_tag
            ));
            if segments > 1 {
                concat_parts.push_str(&format!("[{}]", out_tag));
            }
        }
        
        if segments > 1 {
            filter_complex.push_str(&format!(
                "{}concat=n={}:v=1:a=0[outv]",
                concat_parts,
                segments
            ));
        } else if segments == 0 {
             filter_complex.push_str(&format!(
                "[0:v]trim=start={}:end={},setpts=PTS-STARTPTS[outv]",
                self.trim_start, self.trim_end
            ));
        }

        format!(
            r#"ffmpeg -i "{}" -filter_complex "{}" -map "[outv]" -an -c:v libx264 -crf 18 -pix_fmt yuv420p -preset veryfast -g 30 "{}""#,
            input, filter_complex, output_name
        )
    }

    pub fn calculate_warped_time(&self, musical_time: f64) -> f64 {
        let mut active_beats = self.beats.clone();
        active_beats.retain(|b| b.time >= self.trim_start && b.time <= self.trim_end);
        
        let mut current_musical_time = 0.0;
        for pair in active_beats.windows(2) {
            let seg_start = pair[0].time;
            let seg_end = pair[1].time;
            let weight = pair[0].weight as f64;
            let seg_musical_duration = weight * 0.5;
            
            if musical_time >= current_musical_time && musical_time < (current_musical_time + seg_musical_duration) {
                let progress = (musical_time - current_musical_time) / seg_musical_duration;
                return seg_start + progress * (seg_end - seg_start);
            }
            current_musical_time += seg_musical_duration;
        }
        
        self.trim_start
    }

    pub fn total_warped_duration(&self) -> f64 {
        let mut active_beats = self.beats.clone();
        active_beats.retain(|b| b.time >= self.trim_start && b.time <= self.trim_end);
        
        let mut total = 0.0;
        for pair in active_beats.windows(2) {
            total += (pair[0].weight as f64) * 0.5;
        }
        if total == 0.0 { self.trim_end - self.trim_start } else { total }
    }

    #[cfg(not(target_arch = "wasm32"))]
    fn fetch_frame_native(&mut self, ctx: &egui::Context) {
        let Some(path) = &self.video_path else { return };
        if (self.current_time - self.last_requested_time).abs() < 0.033 { return; }
        self.last_requested_time = self.current_time;

        if self.duration == 10.0 {
            let probe = Command::new("ffprobe")
                .arg("-v").arg("error")
                .arg("-show_entries").arg("format=duration")
                .arg("-of").arg("default=noprint_wrappers=1:nokey=1")
                .arg(path)
                .output();
            if let Ok(out) = probe {
                let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
                if let Ok(d) = s.parse::<f64>() {
                    self.duration = d;
                    self.trim_end = d;
                }
            }
        }

        let output = Command::new("ffmpeg")
            .arg("-ss").arg(format!("{:.3}", self.current_time))
            .arg("-i").arg(path)
            .arg("-frames:v").arg("1")
            .arg("-f").arg("image2pipe")
            .arg("-vcodec").arg("rawvideo")
            .arg("-pix_fmt").arg("rgb24")
            .arg("-s").arg("640x360")
            .arg("-")
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn();

        match output {
            Ok(mut child) => {
                let mut buffer = Vec::new();
                if let Some(mut stdout) = child.stdout.take() {
                    if stdout.read_to_end(&mut buffer).is_ok() {
                        let w = 640; let h = 360;
                        if buffer.len() == w * h * 3 {
                            let image = egui::ColorImage::from_rgb([w, h], &buffer);
                            self.texture = Some(ctx.load_texture("video_frame", image, Default::default()));
                        }
                    }
                }
            }
            Err(e) => {
                log::error!("[NATIVE] Failed to spawn ffmpeg: {}", e);
            }
        }
    }
}

impl eframe::App for BeatMapper {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let dt = ctx.input(|i| i.stable_dt) as f64;

        if ctx.input(|i| i.key_pressed(egui::Key::Space)) {
            self.beats.push(Beat { time: self.current_time, weight: 1 });
            self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
        }

        if self.is_playing {
            if self.is_preview {
                self.steady_time += dt;
                let total_d = self.total_warped_duration();
                if self.steady_time > total_d { self.steady_time = 0.0; }
                self.current_time = self.calculate_warped_time(self.steady_time);
            } else {
                self.current_time += dt;
                if self.current_time > self.trim_end { self.current_time = self.trim_start; }
            }
            ctx.request_repaint();
        }

        #[cfg(not(target_arch = "wasm32"))]
        if !self.ffmpeg_missing {
            self.fetch_frame_native(ctx);
        }

        #[cfg(target_arch = "wasm32")]
        {
            if let Ok(mut state) = SHARED_STATE.lock() {
                if let Some(path) = state.video_path.take() {
                    self.video_path = Some(path);
                    self.last_requested_time = -1.0;
                }
                if let Some((data, size)) = state.frame_data.take() {
                    let image = egui::ColorImage::from_rgba_unmultiplied([size.0, size.1], &data);
                    self.texture = Some(ctx.load_texture("video_frame", image, Default::default()));
                }
                if state.duration > 0.0 && (state.duration - self.duration).abs() > 0.001 {
                    self.duration = state.duration;
                    self.trim_end = state.duration;
                }
                state.current_time = self.current_time;
                state.is_preview = self.is_preview;
            }
            ctx.request_repaint();
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("SKEWER // Rhythmic Media Prep");
            
            ui.collapsing("❓ Quick Guide", |ui| {
                ui.label("1. 📁 Load a video file.");
                ui.label("2. 🔍 Scrub to a visual beat (e.g. a kick drum or hit).");
                ui.label("3. ⌨ Press SPACE or ➕ Anchor to mark it.");
                ui.label("4. 🖱 Click the segment between anchors to set its Musical Weight (how many 0.5s beats it should last).");
                ui.label("5. 🔄 Toggle METRONOME PREVIEW to see your changes in real-time.");
                ui.label("6. 🚀 EXPORT when the rhythm feels perfect.");
            });

            ui.add_space(5.0);
            
            ui.horizontal(|ui| {
                #[cfg(not(target_arch = "wasm32"))]
                if ui.button("📁 Open Video").clicked() {
                    if let Some(path) = FileDialog::new().add_filter("Video", &["mp4", "mov", "mkv", "avi"]).pick_file() {
                        self.video_path = Some(path);
                        self.last_requested_time = -1.0; 
                    }
                }
                #[cfg(target_arch = "wasm32")]
                if ui.button("📁 Load Video").clicked() {
                    if let Ok(mut state) = SHARED_STATE.lock() { state.load_video_clicked = true; }
                }
                
                if let Some(path) = &self.video_path {
                    ui.label(format!("Active: {}", path.file_name().unwrap().to_string_lossy()));
                }
                
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.toggle_value(&mut self.is_preview, "METRONOME PREVIEW");
                    ui.label(format!("Beats: {}", self.beats.len()));
                });
            });

            ui.add_space(10.0);

            if let Some(tex) = &self.texture {
                let width = ui.available_width();
                let tex_size = tex.size_vec2();
                let aspect = tex_size.y / tex_size.x;
                ui.add(egui::Image::new(tex).max_width(width).max_height(width * aspect));
            } else {
                let preview_size = egui::vec2(ui.available_width(), 300.0);
                let (rect, _response) = ui.allocate_at_least(preview_size, egui::Sense::hover());
                ui.painter().rect_filled(rect, 0.0, egui::Color32::BLACK);
                ui.painter().text(rect.center(), egui::Align2::CENTER_CENTER, "NO VIDEO LOADED", egui::FontId::proportional(20.0), egui::Color32::DARK_GRAY);
            }

            ui.add_space(10.0);

            ui.label("Rhythmic Anchor Grid (Space to add Anchor)");
            let mt_size = egui::vec2(ui.available_width(), 60.0);
            let (mt_rect, mt_response) = ui.allocate_at_least(mt_size, egui::Sense::click_and_drag());
            ui.painter().rect_filled(mt_rect, 2.0, var_surface_color());
            
            // Heatmap & Segments
            let mut active_beats = self.beats.clone();
            active_beats.retain(|b| b.time >= self.trim_start && b.time <= self.trim_end);
            
            for (i, pair) in active_beats.windows(2).enumerate() {
                let x1 = mt_rect.left() + (pair[0].time as f32 / self.duration as f32) * mt_rect.width();
                let x2 = mt_rect.left() + (pair[1].time as f32 / self.duration as f32) * mt_rect.width();
                let seg_rect = egui::Rect::from_x_y_ranges(x1..=x2, mt_rect.y_range());
                
                let source_dur = pair[1].time - pair[0].time;
                let target_dur = pair[0].weight as f64 * 0.5;
                let ratio = target_dur / source_dur; // 1.0 is natural
                
                let color = if ratio > 1.2 { egui::Color32::from_rgba_unmultiplied(0, 0, 255, 100) } // Slow/Stretch
                            else if ratio < 0.8 { egui::Color32::from_rgba_unmultiplied(255, 0, 0, 100) } // Fast/Squeeze
                            else { egui::Color32::from_rgba_unmultiplied(0, 255, 0, 100) }; // Natural
                
                ui.painter().rect_filled(seg_rect, 0.0, color);
                
                if ui.interact(seg_rect, ui.make_persistent_id(format!("seg_{}", i)), egui::Sense::click()).clicked() {
                    self.selected_segment = Some(i);
                }
                
                if self.selected_segment == Some(i) {
                    ui.painter().rect_stroke(seg_rect, 0.0, (2.0, egui::Color32::WHITE));
                }
            }

            let mut to_delete = None;
            for i in 0..self.beats.len() {
                let beat_time = self.beats[i].time;
                let x = mt_rect.left() + (beat_time as f32 / self.duration as f32) * mt_rect.width();
                let marker_rect = egui::Rect::from_center_size(egui::pos2(x, mt_rect.center().y), egui::vec2(12.0, 40.0));
                let marker_id = ui.make_persistent_id(format!("marker_{}", i));
                let marker_resp = ui.interact(marker_rect, marker_id, egui::Sense::drag());
                
                if marker_resp.dragged() {
                    let delta_x = marker_resp.drag_delta().x;
                    let delta_time = (delta_x / mt_rect.width()) * self.duration as f32;
                    self.beats[i].time = (self.beats[i].time + delta_time as f64).clamp(0.0, self.duration);
                    self.dragging_beat = Some(i);
                }
                
                marker_resp.context_menu(|ui| {
                    if ui.button("🗑 Delete").clicked() { to_delete = Some(i); ui.close_menu(); }
                });

                let color = if self.dragging_beat == Some(i) { egui::Color32::WHITE } else { var_accent() };
                ui.painter().rect_filled(marker_rect, 1.0, color);
            }

            if let Some(idx) = to_delete { self.beats.remove(idx); }
            if mt_response.drag_stopped() { 
                self.dragging_beat = None; 
                self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
            }

            let playhead_x = mt_rect.left() + (self.current_time as f32 / self.duration as f32) * mt_rect.width();
            ui.painter().line_segment([egui::pos2(playhead_x, mt_rect.top()), egui::pos2(playhead_x, mt_rect.bottom())], (2.0, egui::Color32::YELLOW));

            ui.add_space(10.0);

            ui.horizontal(|ui| {
                let btn_txt = if self.is_playing { "⏸ Pause" } else { "▶ Play" };
                if ui.button(btn_txt).clicked() { self.is_playing = !self.is_playing; }

                if ui.button("⏪ -1f").clicked() { self.current_time = (self.current_time - 0.033).clamp(0.0, self.duration); }
                if ui.button("⏩ +1f").clicked() { self.current_time = (self.current_time + 0.033).clamp(0.0, self.duration); }

                if ui.button("➕ Anchor").clicked() {
                    self.beats.push(Beat { time: self.current_time, weight: 1 });
                    self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
                }
                
                if let Some(idx) = self.dragging_beat {
                    if ui.button("🗑 Delete Active").clicked() { self.beats.remove(idx); self.dragging_beat = None; }
                }
                if ui.button("🗑 Reset All").clicked() { self.beats = vec![Beat { time: 0.0, weight: 1 }]; }
            });

            if let Some(seg_idx) = self.selected_segment {
                if seg_idx < active_beats.len().saturating_sub(1) {
                    ui.group(|ui| {
                        ui.horizontal(|ui| {
                            ui.label(format!("SEGMENT {} Weight (Beats):", seg_idx + 1));
                            let mut weight = active_beats[seg_idx].weight;
                            if ui.add(egui::DragValue::new(&mut weight).clamp_range(1..=64)).changed() {
                                let b_time = active_beats[seg_idx].time;
                                if let Some(b) = self.beats.iter_mut().find(|b| (b.time - b_time).abs() < 0.001) {
                                    b.weight = weight;
                                }
                            }
                            if ui.button("1b").clicked() { 
                                let b_time = active_beats[seg_idx].time;
                                if let Some(b) = self.beats.iter_mut().find(|b| (b.time - b_time).abs() < 0.001) { b.weight = 1; }
                            }
                            if ui.button("4b").clicked() { 
                                let b_time = active_beats[seg_idx].time;
                                if let Some(b) = self.beats.iter_mut().find(|b| (b.time - b_time).abs() < 0.001) { b.weight = 4; }
                            }
                        });
                    });
                }
            }

            ui.add_space(10.0);
            ui.group(|ui| {
                ui.horizontal(|ui| {
                    ui.label("Trim Start:");
                    if ui.add(egui::DragValue::new(&mut self.trim_start).speed(0.1).clamp_range(0.0..=self.trim_end)).changed() {
                         self.beats.retain(|b| b.time >= self.trim_start);
                    }
                    ui.label("End:");
                    if ui.add(egui::DragValue::new(&mut self.trim_end).speed(0.1).clamp_range(self.trim_start..=self.duration)).changed() {
                         self.beats.retain(|b| b.time <= self.trim_end);
                    }
                });
            });

            ui.add_space(20.0);
            
            #[cfg(not(target_arch = "wasm32"))]
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

            #[cfg(target_arch = "wasm32")]
            ui.vertical_centered(|ui| {
                if ui.add_sized([ui.available_width(), 40.0], egui::Button::new("🚀 RENDER & EXPORT IN BROWSER")).clicked() {
                    let cmd = self.generate_ffmpeg_cmd_string("warped_loop.mp4");
                    if let Ok(mut state) = SHARED_STATE.lock() {
                        state.export_clicked = true;
                        state.last_generated_cmd = cmd;
                    }
                }
                if ui.button("📋 COPY FFMPEG COMMAND").clicked() {
                    self.last_ffmpeg_cmd = self.generate_ffmpeg_cmd_string("warped_loop.mp4");
                    ui.output_mut(|o| o.copied_text = self.last_ffmpeg_cmd.clone());
                }
                if !self.last_ffmpeg_cmd.is_empty() { ui.code(&self.last_ffmpeg_cmd); }
            });
        });
    }
}

fn var_surface_color() -> egui::Color32 { egui::Color32::from_rgb(26, 26, 26) }
#[allow(dead_code)]
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
    pub fn load_video(&self, path: &str) {
        if let Ok(mut state) = SHARED_STATE.lock() {
            state.video_path = Some(PathBuf::from(path));
        }
    }

    #[wasm_bindgen]
    pub fn set_duration(&self, duration: f64) {
        if let Ok(mut state) = SHARED_STATE.lock() {
            state.duration = duration;
        }
    }

    #[wasm_bindgen]
    pub fn is_load_clicked(&self) -> bool {
        if let Ok(state) = SHARED_STATE.lock() {
            state.load_video_clicked
        } else {
            false
        }
    }

    #[wasm_bindgen]
    pub fn reset_load_clicked(&self) {
        if let Ok(mut state) = SHARED_STATE.lock() {
            state.load_video_clicked = false;
        }
    }

    #[wasm_bindgen]
    pub fn get_current_time(&self) -> f64 {
        if let Ok(state) = SHARED_STATE.lock() {
            state.current_time
        } else {
            0.0
        }
    }

    #[wasm_bindgen]
    pub fn get_ffmpeg_command(&self) -> String {
        if let Ok(mut state) = SHARED_STATE.lock() {
            if state.export_clicked {
                state.export_clicked = false;
                return state.last_generated_cmd.clone();
            }
        }
        "".to_string()
    }

    #[wasm_bindgen]
    pub fn push_frame(&self, rgba_data: &[u8], width: usize, height: usize) {
        if let Ok(mut state) = SHARED_STATE.lock() {
            state.frame_data = Some((rgba_data.to_vec(), (width, height)));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ffmpeg_cmd_generation_single_beat() {
        let mut app = BeatMapper::default();
        app.video_path = Some(PathBuf::from("test.mp4"));
        app.beats = vec![Beat { time: 0.0, weight: 1 }];
        let cmd = app.generate_ffmpeg_cmd_string("out.mp4");
        assert!(cmd.contains("setpts=PTS-STARTPTS[outv]"));
    }

    #[test]
    fn test_ffmpeg_cmd_generation_two_beats() {
        let mut app = BeatMapper::default();
        app.video_path = Some(PathBuf::from("my_vid.mp4"));
        app.beats = vec![Beat { time: 1.0, weight: 1 }, Beat { time: 2.0, weight: 1 }];
        let cmd = app.generate_ffmpeg_cmd_string("out.mp4");
        // Target is 0.5s, source is 1.0s -> multiplier 0.5
        assert!(cmd.contains("setpts=0.500000*(PTS-STARTPTS)"));
    }

    #[test]
    fn test_ffmpeg_cmd_generation_three_beats_weighted() {
        let mut app = BeatMapper::default();
        app.video_path = Some(PathBuf::from("my_vid.mp4"));
        // Segment 1: 0.0 to 1.0 (dur 1.0), weight 4 (target 2.0s) -> multiplier 2.0
        // Segment 2: 1.0 to 1.5 (dur 0.5), weight 1 (target 0.5s) -> multiplier 1.0
        app.beats = vec![
            Beat { time: 0.0, weight: 4 }, 
            Beat { time: 1.0, weight: 1 }, 
            Beat { time: 1.5, weight: 1 }
        ];
        let cmd = app.generate_ffmpeg_cmd_string("out.mp4");
        assert!(cmd.contains("setpts=2.000000*(PTS-STARTPTS)[v0]"));
        assert!(cmd.contains("setpts=1.000000*(PTS-STARTPTS)[v1]"));
    }

    #[test]
    fn test_total_warped_duration() {
        let mut app = BeatMapper::default();
        app.trim_start = 0.0;
        app.trim_end = 10.0;
        app.beats = vec![
            Beat { time: 0.0, weight: 4 }, 
            Beat { time: 1.0, weight: 8 }, 
            Beat { time: 2.0, weight: 1 }
        ];
        // Seg 1: 4 beats (2.0s)
        // Seg 2: 8 beats (4.0s)
        // Total should be 6.0s
        assert_eq!(app.total_warped_duration(), 6.0);
    }

    #[test]
    fn test_trim_logic_consistency() {
        let mut app = BeatMapper::default();
        app.beats = vec![
            Beat { time: 0.0, weight: 1 },
            Beat { time: 1.0, weight: 4 },
            Beat { time: 2.0, weight: 1 },
            Beat { time: 3.0, weight: 1 }
        ];
        
        // Trim to only include the middle segment
        app.trim_start = 0.5;
        app.trim_end = 2.5;
        
        // Active beats should be [1.0, 2.0]
        // One segment: 4 beats (2.0s)
        assert_eq!(app.total_warped_duration(), 2.0);
        
        // Musical time 1.0 (halfway through the 2.0s warped duration)
        // Should map to 1.5s in source
        let t = app.calculate_warped_time(1.0);
        assert!((t - 1.5).abs() < 0.001);
    }

    #[test]
    fn test_beat_serialization() {
        let beats = vec![Beat { time: 1.23, weight: 4 }];
        let json = serde_json::to_string(&beats).unwrap();
        let decoded: Vec<Beat> = serde_json::from_str(&json).unwrap();
        assert_eq!(beats, decoded);
    }

    #[test]
    fn test_anchor_sorting() {
        let mut beats = vec![
            Beat { time: 5.0, weight: 1 },
            Beat { time: 1.0, weight: 1 },
            Beat { time: 3.0, weight: 1 }
        ];
        beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
        assert_eq!(beats[0].time, 1.0);
        assert_eq!(beats[1].time, 3.0);
        assert_eq!(beats[2].time, 5.0);
    }

    #[test]
    fn test_boundary_mapping() {
        let mut app = BeatMapper::default();
        app.trim_start = 0.0;
        app.trim_end = 10.0;
        app.beats = vec![
            Beat { time: 0.0, weight: 2 }, // 1.0s musical
            Beat { time: 1.0, weight: 2 }
        ];
        
        // At exactly 1.0s musical, it should be at 1.0s source
        assert!((app.calculate_warped_time(1.0) - 1.0).abs() < 0.001);
        
        // Slightly before end
        assert!((app.calculate_warped_time(0.99) - 0.99).abs() < 0.001);
    }
}
