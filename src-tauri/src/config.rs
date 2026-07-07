use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    #[serde(default = "default_download_dir")]
    pub download_dir: String,

    #[serde(default = "default_port")]
    pub default_port: u16,

    #[serde(default = "default_theme")]
    pub theme: String,   // "system" | "light" | "dark"

    #[serde(default = "default_auto_discover")]
    pub auto_discover: bool,

    #[serde(default = "default_overwrite")]
    pub overwrite_policy: String,  // "overwrite" | "rename" | "skip"
}

fn default_download_dir() -> String { String::new() }
fn default_port() -> u16 { 8080 }
fn default_theme() -> String { "system".into() }
fn default_auto_discover() -> bool { false }
fn default_overwrite() -> String { "rename".into() }

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            download_dir:    String::new(),
            default_port:    8080,
            theme:           "system".into(),
            auto_discover:   false,
            overwrite_policy: "rename".into(),
        }
    }
}

impl AppConfig {
    fn config_path(app_data: &PathBuf) -> PathBuf {
        app_data.join("config.json")
    }

    pub fn load(app_data: &PathBuf) -> Self {
        let path = Self::config_path(app_data);
        match std::fs::read_to_string(&path) {
            Ok(content) => serde_json::from_str(&content).unwrap_or_default(),
            Err(_) => Self::default(),
        }
    }

    pub fn save(&self, app_data: &PathBuf) {
        let path = Self::config_path(app_data);
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        if let Ok(json) = serde_json::to_string_pretty(self) {
            let _ = std::fs::write(&path, json);
        }
    }
}
