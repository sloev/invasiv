#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use eframe::egui;
use rfd::FileDialog;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::process::Command;

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
struct Beat {
    time: f64, // Time in seconds in the original video
}

struct BeatMapper {
    video_path: Option<PathBuf>,
    beats: Vec<Beat>,
    current_time: f64,
    duration: f64,
    _is_playing: bool,
    exporting: bool,
    export_progress: f32,
}

impl Default for BeatMapper {
    fn default() -> Self {
        Self {
            video_path: None,
            beats: vec![Beat { time: 0.0 }],
            current_time: 0.0,
            duration: 10.0, // Default until loaded
            _is_playing: false,
            exporting: false,
            export_progress: 0.0,
        }
    }
}

impl BeatMapper {
    fn generate_ffmpeg_cmd(&self, output_path: &PathBuf) -> Option<Command> {
        let input = self.video_path.as_ref()?;
        let mut cmd = Command::new("ffmpeg");
        
        // Non-linear time warping logic:
        // For each segment [b_i, b_{i+1}], we want it to be 2.0 seconds in output.
        // We use the 'trim' and 'setpts' filters for each segment and then concat.
        
        let mut filter_complex = String::new();
        let mut concat_parts = String::new();
        
        for (i, pair) in self.beats.windows(2).enumerate() {
            let start = pair[0].time;
            let end = pair[1].time;
            let segment_duration = end - start;
            if segment_duration <= 0.0 { continue; }
            
            // Calculate PTS scaling: desired_duration / actual_duration
            let scale = 2.0 / segment_duration;
            
            filter_complex.push_str(&format!(
                "[0:v]trim=start={}:end={},setpts={:.6}*(PTS-STARTPTS)[v{}];",
                start, end, 1.0 / scale, i
            ));
            concat_parts.push_str(&format!("[v{}]", i));
        }
        
        filter_complex.push_str(&format!(
            "{}concat=n={}:v=1:a=0[outv]",
            concat_parts,
            self.beats.len() - 1
        ));

        cmd.arg("-y")
           .arg("-i").arg(input)
           .arg("-filter_complex").arg(filter_complex)
           .arg("-map").arg("[outv]")
           .arg("-an") // No audio
           .arg("-c:v").arg("libx264")
           .arg("-crf").arg("18")
           .arg("-preset").arg("veryfast")
           .arg("-g").arg("30") // Short GOP for better seeking
           .arg(output_path);
           
        Some(cmd)
    }
}

impl eframe::App for BeatMapper {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("SKEWER // Rhythmic Warper");
            ui.add_space(10.0);

            ui.horizontal(|ui| {
                if ui.button("📁 Open Video").clicked() {
                    if let Some(path) = FileDialog::new().add_filter("Video", &["mp4", "mov", "mkv", "avi"]).pick_file() {
                        self.video_path = Some(path);
                        // In a real app, we'd use a library to get the actual duration here.
                    }
                }
                if let Some(path) = &self.video_path {
                    ui.label(format!("Active: {}", path.file_name().unwrap().to_string_lossy()));
                }
            });

            ui.add_space(20.0);

            // Timeline Visualization
            let (rect, _response) = ui.allocate_at_least(
                egui::vec2(ui.available_width(), 100.0),
                egui::Sense::click_and_drag()
            );
            
            ui.painter().rect_filled(rect, 2.0, var_surface_color());
            
            // Draw ticks every 1 second
            for sec in 0..=(self.duration as i32) {
                let x = rect.left() + (sec as f32 / self.duration as f32) * rect.width();
                ui.painter().line_segment(
                    [egui::pos2(x, rect.top()), egui::pos2(x, rect.top() + 10.0)],
                    (1.0, var_text_dim())
                );
            }

            // Draw Beats
            for i in 0..self.beats.len() {
                let beat_time = self.beats[i].time;
                let x = rect.left() + (beat_time as f32 / self.duration as f32) * rect.width();
                
                let beat_rect = egui::Rect::from_center_size(
                    egui::pos2(x, rect.center().y),
                    egui::vec2(10.0, 40.0)
                );
                
                let beat_id = ui.make_persistent_id(format!("beat_{}", i));
                let beat_resp = ui.interact(beat_rect, beat_id, egui::Sense::drag());
                
                if beat_resp.dragged() {
                    let delta_x = beat_resp.drag_delta().x;
                    let delta_time = (delta_x / rect.width()) * self.duration as f32;
                    self.beats[i].time = (self.beats[i].time + delta_time as f64).clamp(0.0, self.duration);
                }

                ui.painter().rect_filled(beat_rect, 2.0, var_accent());
                ui.painter().text(
                    beat_rect.left_top(),
                    egui::Align2::LEFT_BOTTOM,
                    format!("{:.2}s", beat_time),
                    egui::FontId::monospace(10.0),
                    egui::Color32::WHITE
                );
            }

            ui.add_space(20.0);

            ui.horizontal(|ui| {
                if ui.button("➕ Add Beat at Current").clicked() {
                    self.beats.push(Beat { time: self.current_time });
                    self.beats.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap());
                }
                if ui.button("🗑 Reset").clicked() {
                    self.beats = vec![Beat { time: 0.0 }];
                }
            });

            ui.add_space(40.0);
            
            if self.exporting {
                ui.add(egui::ProgressBar::new(self.export_progress).text("Warping Video..."));
            } else {
                if ui.add_sized([ui.available_width(), 40.0], egui::Button::new("🚀 EXPORT WARPED VIDEO")).clicked() {
                    if let Some(output) = FileDialog::new().set_file_name("warped_loop.mp4").save_file() {
                        if let Some(mut cmd) = self.generate_ffmpeg_cmd(&output) {
                            println!("Executing: {:?}", cmd);
                            // Real async execution would happen here
                            match cmd.status() {
                                Ok(s) => println!("Success: {}", s),
                                Err(e) => println!("Error: {}", e),
                            }
                        }
                    }
                }
            }
            
            ui.with_layout(egui::Layout::bottom_up(egui::Align::Center), |ui| {
                ui.label("PRO TIP: Every beat interval will become exactly 2.0 seconds in the output.");
            });
        });
    }
}

// Helper colors to match Invasiv aesthetic
fn var_surface_color() -> egui::Color32 { egui::Color32::from_rgb(26, 26, 26) }
fn var_text_dim() -> egui::Color32 { egui::Color32::from_rgb(102, 102, 102) }
fn var_accent() -> egui::Color32 { egui::Color32::from_rgb(255, 59, 48) }

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([800.0, 600.0]),
        ..Default::default()
    };
    eframe::run_native(
        "SKEWER",
        options,
        Box::new(|_cc| Box::new(BeatMapper::default())),
    )
}
