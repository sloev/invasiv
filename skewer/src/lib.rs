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

#[derive(Default)]
pub struct AppState {
    pub video_path: Option<PathBuf>,
    pub load_video_clicked: bool,
    pub frame_data: Option<(Vec<u8>, (usize, usize))>,
    pub current_time: f64,
    pub duration: f64,
}

lazy_static::lazy_static! {
    static ref SHARED_STATE: Arc<Mutex<AppState>> = Arc::new(Mutex::new(AppState::default()));
}

pub struct BeatMapper {
    pub video_path: Option<PathBuf>,
    pub beats: Vec<Beat>,
    pub current_time: f64,
    pub duration: f64,
    pub trim_start: f64,
    pub trim_end: f64,
    pub is_playing: bool,
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
            trim_start: 0.0,
            trim_end: 10.0,
            is_playing: false,
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
        
        // Ensure markers at start and end of trim if not present? 
        // Or just use the existing beats.
        
        let segments = active_beats.len().saturating_sub(1);
        
        for (i, pair) in active_beats.windows(2).enumerate() {
            let start = pair[0].time;
            let end = pair[1].time;
            let segment_duration = end - start;
            if segment_duration <= 0.001 { continue; }
            let scale = 2.0 / segment_duration;
            
            let out_tag = if segments == 1 { "outv".to_string() } else { format!("v{}", i) };
            
            filter_complex.push_str(&format!(
                "[0:v]trim=start={}:end={},setpts={:.6}*(PTS-STARTPTS)[{}];",
                start, end, 1.0 / scale, out_tag
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
             // Just trim the whole thing if no beats?
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

    #[cfg(not(target_arch = "wasm32"))]
    fn fetch_frame_native(&mut self, ctx: &egui::Context) {
        let Some(path) = &self.video_path else { return };
        if (self.current_time - self.last_requested_time).abs() < 0.033 { return; }
        self.last_requested_time = self.current_time;

        // Try to probe duration if it's still default
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
                    log::info!("[NATIVE] Probed duration: {}s", d);
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
            .arg("-s").arg("640x360") // Force a consistent size for the preview
            .arg("-")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped()) // Capture stderr for debugging
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
        if self.is_playing {
            self.current_time += ctx.input(|i| i.stable_dt) as f64;
            if self.current_time > self.trim_end {
                self.current_time = self.trim_start;
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
            }
            ctx.request_repaint();
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
                            log::info!("[NATIVE] Selected file: {:?}", path);
                            self.video_path = Some(path);
                            self.last_requested_time = -1.0; 
                        }
                    }
                }
                #[cfg(target_arch = "wasm32")]
                {
                    if ui.button("📁 Load Video").clicked() {
                        if let Ok(mut state) = SHARED_STATE.lock() {
                            state.load_video_clicked = true;
                        }
                    }
                }
                if let Some(path) = &self.video_path {
                    ui.label(format!("Active: {}", path.file_name().unwrap().to_string_lossy()));
                }
                
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
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

            // --- Timeline 1: Video Scrubbing ---
            ui.label("Video Timeline");
            let vt_size = egui::vec2(ui.available_width(), 20.0);
            let (vt_rect, vt_response) = ui.allocate_at_least(vt_size, egui::Sense::click_and_drag());
            ui.painter().rect_filled(vt_rect, 2.0, var_surface_color());
            
            if vt_response.dragged() || vt_response.clicked() {
                if let Some(pos) = vt_response.interact_pointer_pos() {
                    let x = pos.x - vt_rect.left();
                    self.current_time = ((x / vt_rect.width()) as f64 * self.duration).clamp(0.0, self.duration);
                }
            }
            let playhead_x = vt_rect.left() + (self.current_time as f32 / self.duration as f32) * vt_rect.width();
            ui.painter().line_segment([egui::pos2(playhead_x, vt_rect.top()), egui::pos2(playhead_x, vt_rect.bottom())], (2.0, egui::Color32::WHITE));

            ui.add_space(5.0);

            // --- Timeline 2: Beat Markers ---
            ui.label("Beat Markers");
            let mt_size = egui::vec2(ui.available_width(), 40.0);
            let (mt_rect, mt_response) = ui.allocate_at_least(mt_size, egui::Sense::click_and_drag());
            ui.painter().rect_filled(mt_rect, 2.0, egui::Color32::from_rgb(35, 35, 35));
            
            // Draw trim overlay
            let trim_start_x = mt_rect.left() + (self.trim_start as f32 / self.duration as f32) * mt_rect.width();
            let trim_end_x = mt_rect.left() + (self.trim_end as f32 / self.duration as f32) * mt_rect.width();
            ui.painter().rect_filled(egui::Rect::from_x_y_ranges(mt_rect.left()..=trim_start_x, mt_rect.y_range()), 0.0, egui::Color32::from_rgba_unmultiplied(255, 0, 0, 40));
            ui.painter().rect_filled(egui::Rect::from_x_y_ranges(trim_end_x..=mt_rect.right(), mt_rect.y_range()), 0.0, egui::Color32::from_rgba_unmultiplied(255, 0, 0, 40));

            let mut to_delete = None;

            for i in 0..self.beats.len() {
                let beat_time = self.beats[i].time;
                let x = mt_rect.left() + (beat_time as f32 / self.duration as f32) * mt_rect.width();
                let marker_rect = egui::Rect::from_center_size(egui::pos2(x, mt_rect.center().y), egui::vec2(12.0, 30.0));
                let marker_id = ui.make_persistent_id(format!("marker_{}", i));
                let marker_resp = ui.interact(marker_rect, marker_id, egui::Sense::drag());
                
                if marker_resp.dragged() {
                    let delta_x = marker_resp.drag_delta().x;
                    let delta_time = (delta_x / mt_rect.width()) * self.duration as f32;
                    self.beats[i].time = (self.beats[i].time + delta_time as f64).clamp(0.0, self.duration);
                    self.dragging_beat = Some(i);
                }
                
                marker_resp.context_menu(|ui| {
                    if ui.button("🗑 Delete").clicked() {
                        to_delete = Some(i);
                        ui.close_menu();
                    }
                });

                let color = if self.dragging_beat == Some(i) { egui::Color32::WHITE } else { var_accent() };
                ui.painter().rect_filled(marker_rect, 1.0, color);
            }

            if let Some(idx) = to_delete {
                self.beats.remove(idx);
            }

            if mt_response.drag_stopped() { 
                self.dragging_beat = None; 
                self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
            }

            ui.add_space(10.0);

            ui.horizontal(|ui| {
                let play_btn_text = if self.is_playing { "⏸ Pause" } else { "▶ Play" };
                if ui.button(play_btn_text).clicked() {
                    self.is_playing = !self.is_playing;
                }

                if ui.button("➕ Beat").clicked() {
                    self.beats.push(Beat { time: self.current_time });
                    self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
                }
                if ui.button("🗑 Reset").clicked() {
                    self.beats = vec![Beat { time: 0.0 }];
                }
                ui.add(egui::Slider::new(&mut self.current_time, 0.0..=self.duration).text("Scrub"));
            });

            ui.add_space(10.0);
            
            ui.group(|ui| {
                ui.label("Trim Settings");
                ui.horizontal(|ui| {
                    ui.label("Start:");
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
        app.beats = vec![Beat { time: 0.0 }];
        let cmd = app.generate_ffmpeg_cmd_string("out.mp4");
        // No segments because only one beat
        assert_eq!(cmd, r#"ffmpeg -i "test.mp4" -filter_complex "" -map "[outv]" -an -c:v libx264 -crf 18 -preset veryfast -g 30 "out.mp4""#);
    }

    #[test]
    fn test_ffmpeg_cmd_generation_two_beats() {
        let mut app = BeatMapper::default();
        app.video_path = Some(PathBuf::from("my_vid.mp4"));
        app.beats = vec![Beat { time: 1.0 }, Beat { time: 2.0 }];
        let cmd = app.generate_ffmpeg_cmd_string("out.mp4");
        // Segment duration is 1.0, scale is 2.0/1.0 = 2.0
        // 1.0 / scale = 0.5
        assert_eq!(cmd, r#"ffmpeg -i "my_vid.mp4" -filter_complex "[0:v]trim=start=1:end=2,setpts=0.500000*(PTS-STARTPTS)[outv];" -map "[outv]" -an -c:v libx264 -crf 18 -preset veryfast -g 30 "out.mp4""#);
    }

    #[test]
    fn test_ffmpeg_cmd_generation_three_beats() {
        let mut app = BeatMapper::default();
        app.video_path = Some(PathBuf::from("my_vid.mp4"));
        app.beats = vec![Beat { time: 0.0 }, Beat { time: 1.0 }, Beat { time: 1.5 }];
        let cmd = app.generate_ffmpeg_cmd_string("out.mp4");
        // Segment 1: 0.0 to 1.0 -> duration 1.0, scale 2.0, pts 0.5
        // Segment 2: 1.0 to 1.5 -> duration 0.5, scale 4.0, pts 0.25
        assert!(cmd.contains("setpts=0.500000*(PTS-STARTPTS)[v0]"));
        assert!(cmd.contains("setpts=0.250000*(PTS-STARTPTS)[v1]"));
        assert!(cmd.contains("concat=n=2:v=1:a=0[outv]"));
    }

    #[test]
    fn test_app_state_initialization() {
        let state = AppState::default();
        assert_eq!(state.current_time, 0.0);
        assert_eq!(state.duration, 0.0);
        assert_eq!(state.load_video_clicked, false);
    }

    #[test]
    fn test_shared_state_interaction() {
        let state = SHARED_STATE.lock().unwrap();
        assert_eq!(state.current_time, 0.0);
    }

    #[test]
    fn test_beat_mapper_initialization() {
        let mapper = BeatMapper::default();
        assert_eq!(mapper.beats.len(), 1);
        assert_eq!(mapper.beats[0].time, 0.0);
        assert_eq!(mapper.current_time, 0.0);
    }

    #[test]
    #[cfg(not(target_arch = "wasm32"))]
    fn test_fetch_frame_native_execution() {
        if Command::new("ffmpeg").arg("-version").output().is_err() { return; }
        let output = Command::new("ffmpeg")
            .arg("-ss").arg("1.000")
            .arg("-i").arg("test_vid.mp4")
            .arg("-frames:v").arg("1")
            .arg("-f").arg("image2pipe")
            .arg("-vcodec").arg("rawvideo")
            .arg("-pix_fmt").arg("rgb24")
            .arg("-s").arg("640x360")
            .arg("-")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();

        let mut child = output;
        let mut buffer = Vec::new();
        if let Some(mut stdout) = child.stdout.take() { stdout.read_to_end(&mut buffer).unwrap(); }
        let mut stderr_str = String::new();
        if let Some(mut stderr) = child.stderr.take() { stderr.read_to_string(&mut stderr_str).unwrap(); }

        assert_eq!(buffer.len(), 640 * 360 * 3, "Buffer size mismatch. Stderr: {}", stderr_str);
    }
}
