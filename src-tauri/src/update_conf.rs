// ============================================================
// Auto-update configuration
// Edit these values, then rebuild.
// ============================================================

/// Set to `true` for local development, `false` for production.
pub const DEV_MODE: bool = false;

/// -------- production: multiple mirrors for updateLists.json --------
/// The fastest (lowest TCP latency) will be selected automatically.
pub const UPDATE_LISTS_URLS: &[&str] = &[
    "https://raw.githubusercontent.com/szWrn/efly/main/updateLists.json",
    "https://raw.giteeusercontent.com/szWrn/efly/main/updateLists.json",
];

/// -------- dev: local HTTP server --------
/// Serve with:  cd D:\efly && python -m http.server 9000
pub const DEV_UPDATE_LISTS_URLS: &[&str] = &[
    "http://localhost:9000/updateLists.json",
];

/// Current application version, passed to the DLL via setCurrentVersion().
/// In dev mode this is read from `version.json` at runtime.
/// In production this should be the hardcoded release version.
pub const CURRENT_VERSION: &str = "1.0.0";

/// Read the current version from version.json. Falls back to CURRENT_VERSION.
pub fn effective_version() -> String {
    use serde_json::Value;
    if DEV_MODE {
        if let Ok(content) = std::fs::read_to_string("version.json") {
            if let Ok(parsed) = serde_json::from_str::<Value>(&content) {
                if let Some(ver) = parsed.get("version").and_then(|v| v.as_str()) {
                    return ver.to_string();
                }
            }
        }
    }
    CURRENT_VERSION.to_string()
}

/// All candidate URLs for updateLists.json (dev or prod).
pub fn candidate_urls() -> &'static [&'static str] {
    if DEV_MODE {
        DEV_UPDATE_LISTS_URLS
    } else {
        UPDATE_LISTS_URLS
    }
}

// ============================================================
// Priority behaviour (used by the frontend to decide UX)
// ============================================================
//
// "silent" → download & install without asking the user
// "warn"   → show a dialog; user may accept or dismiss
// "force"  → show a dialog; user must accept or quit the app
