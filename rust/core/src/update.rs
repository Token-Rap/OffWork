//! Port of `src/update_info.cpp` plus the download checks the Rust shell adds:
//! semver comparison, strict release parsing for an exact asset name and the
//! official HTTPS location, check backoff, progress and SHA-256 manifests.

use serde::de::{self, Deserialize, Deserializer, MapAccess, SeqAccess, Visitor};
use std::cmp::Ordering;

pub const REPO: &str = "Token-Rap/OffWork";
const LEGACY_REPO: &str = "alertform/OffWork";
pub const LATEST_URL: &str = "https://github.com/Token-Rap/OffWork/releases/latest";
pub const LATEST_API: &str = "https://api.github.com/repos/Token-Rap/OffWork/releases/latest";
pub const CHECKSUMS_ASSET: &str = "SHA256SUMS";
pub const MAX_JSON_BYTES: usize = 2 * 1024 * 1024;
pub const MAX_DOWNLOAD_BYTES: u64 = 256 * 1024 * 1024;
const HOSTS: [&str; 4] = [
    "github.com",
    "api.github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
];

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReleaseInfo {
    pub tag: String,
    pub asset_url: String,
    pub checksums_url: String,
}

struct Version<'a> {
    core: [&'a str; 3],
    pre: Vec<&'a str>,
}

fn digits(s: &str) -> bool {
    !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit())
}

fn compare_number(a: &str, b: &str) -> Ordering {
    a.len().cmp(&b.len()).then_with(|| a.cmp(b))
}

fn identifiers(s: &str, pre: bool) -> Option<Vec<&str>> {
    s.split('.')
        .map(|id| {
            let ok = !id.is_empty()
                && id.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
                && !(pre && digits(id) && id.len() > 1 && id.starts_with('0'));
            ok.then_some(id)
        })
        .collect()
}

fn parse_version(s: &str) -> Option<Version<'_>> {
    let s = s.strip_prefix('v').unwrap_or(s);
    let s = match s.split_once('+') {
        Some((head, build)) => {
            identifiers(build, false)?;
            head
        }
        None => s,
    };
    let (s, pre) = match s.split_once('-') {
        Some((head, pre)) => (head, identifiers(pre, true)?),
        None => (s, Vec::new()),
    };
    let parts: Vec<&str> = s.split('.').collect();
    if parts.len() != 3
        || parts
            .iter()
            .any(|n| !digits(n) || (n.len() > 1 && n.starts_with('0')))
    {
        return None;
    }
    Some(Version {
        core: [parts[0], parts[1], parts[2]],
        pre,
    })
}

/// Invalid versions compare as "no update". Accepts an optional leading `v`.
pub fn is_newer(candidate: &str, current: &str) -> bool {
    let (Some(a), Some(b)) = (parse_version(candidate), parse_version(current)) else {
        return false;
    };
    for i in 0..3 {
        match compare_number(a.core[i], b.core[i]) {
            Ordering::Equal => {}
            other => return other == Ordering::Greater,
        }
    }
    if a.pre.is_empty() || b.pre.is_empty() {
        return a.pre.is_empty() && !b.pre.is_empty();
    }
    for (x, y) in a.pre.iter().zip(&b.pre) {
        if x == y {
            continue;
        }
        if digits(x) != digits(y) {
            return !digits(x);
        }
        return if digits(x) {
            compare_number(x, y) == Ordering::Greater
        } else {
            x > y
        };
    }
    a.pre.len() > b.pre.len()
}

pub fn is_stable_tag(tag: &str) -> bool {
    parse_version(tag).is_some_and(|v| v.pre.is_empty())
}

/// JSON value that rejects duplicate object keys, so release notes or a
/// second `tag_name` can never be mistaken for the real field.
enum Json {
    Other,
    Bool(bool),
    Str(String),
    Arr(Vec<Json>),
    Obj(Vec<(String, Json)>),
}

impl Json {
    fn get(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Obj(items) => items.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }
    fn str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s),
            _ => None,
        }
    }
}

impl<'de> Deserialize<'de> for Json {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Self, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = Json;
            fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                f.write_str("JSON")
            }
            fn visit_bool<E>(self, v: bool) -> Result<Json, E> {
                Ok(Json::Bool(v))
            }
            fn visit_i64<E>(self, _: i64) -> Result<Json, E> {
                Ok(Json::Other)
            }
            fn visit_u64<E>(self, _: u64) -> Result<Json, E> {
                Ok(Json::Other)
            }
            fn visit_f64<E>(self, _: f64) -> Result<Json, E> {
                Ok(Json::Other)
            }
            fn visit_unit<E>(self) -> Result<Json, E> {
                Ok(Json::Other)
            }
            fn visit_str<E>(self, v: &str) -> Result<Json, E> {
                Ok(Json::Str(v.to_owned()))
            }
            fn visit_string<E>(self, v: String) -> Result<Json, E> {
                Ok(Json::Str(v))
            }
            fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Json, A::Error> {
                let mut items = Vec::new();
                while let Some(item) = seq.next_element()? {
                    items.push(item);
                }
                Ok(Json::Arr(items))
            }
            fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Json, A::Error> {
                let mut items: Vec<(String, Json)> = Vec::new();
                while let Some((key, value)) = map.next_entry::<String, Json>()? {
                    if items.iter().any(|(k, _)| *k == key) {
                        return Err(de::Error::custom("duplicate key"));
                    }
                    items.push((key, value));
                }
                Ok(Json::Obj(items))
            }
        }
        d.deserialize_any(V)
    }
}

fn download_url(repo: &str, tag: &str, asset: &str) -> String {
    format!("https://github.com/{repo}/releases/download/{tag}/{asset}")
}

fn release(tag: &str, asset_url: String) -> ReleaseInfo {
    ReleaseInfo {
        tag: tag.to_owned(),
        asset_url,
        checksums_url: download_url(REPO, tag, CHECKSUMS_ASSET),
    }
}

/// Only a stable, non-draft release with the exact asset at the official URL.
pub fn parse_release_info(json: &str, asset: &str) -> Option<ReleaseInfo> {
    if json.len() > MAX_JSON_BYTES {
        return None;
    }
    let root: Json = serde_json::from_str(json).ok()?;
    for flag in ["draft", "prerelease"] {
        if let Some(v) = root.get(flag) {
            if !matches!(v, Json::Bool(false)) {
                return None;
            }
        }
    }
    let tag = root.get("tag_name")?.str()?;
    let Json::Arr(assets) = root.get("assets")? else {
        return None;
    };
    if !is_stable_tag(tag) {
        return None;
    }
    let expected = [REPO, LEGACY_REPO].map(|repo| download_url(repo, tag, asset));
    assets.iter().find_map(|a| {
        let name = a.get("name")?.str()?;
        let url = a.get("browser_download_url")?.str()?;
        (name == asset && expected.iter().any(|e| e == url)).then(|| release(tag, url.to_owned()))
    })
}

/// The `Location` of `/releases/latest`: exact official repository path,
/// absolute HTTPS or root-relative, and a stable `vX.Y.Z` tag. No decoding.
pub fn parse_release_location(location: &str, asset: &str) -> Option<ReleaseInfo> {
    let path = location
        .strip_prefix("https://github.com")
        .unwrap_or(location);
    let tag = [REPO, LEGACY_REPO]
        .iter()
        .find_map(|repo| path.strip_prefix(&format!("/{repo}/releases/tag/")))?;
    if !tag.starts_with('v')
        || !tag
            .bytes()
            .all(|b| b == b'v' || b == b'.' || b.is_ascii_digit())
    {
        return None;
    }
    parse_version(tag)?;
    Some(release(tag, download_url(REPO, tag, asset)))
}

/// Success every 6 hours; failures back off 10, 20, 40, 80, 160, then 320 minutes.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct CheckSchedule {
    next_check: u64,
    failures: u32,
}

impl CheckSchedule {
    pub fn due(&self, now_ms: u64) -> bool {
        now_ms >= self.next_check
    }
    pub fn complete(&self, now_ms: u64, succeeded: bool) -> CheckSchedule {
        const MINUTE: u64 = 60_000;
        let failures = if succeeded {
            0
        } else {
            (self.failures + 1).min(6)
        };
        let delay = if succeeded {
            360 * MINUTE
        } else {
            (10 * MINUTE) << (failures - 1)
        };
        CheckSchedule {
            next_check: now_ms.saturating_add(delay),
            failures,
        }
    }
}

/// -1 for an unknown/zero length; 100 is reserved for a verified, flushed file.
pub fn download_percent(received: u64, total: Option<u64>) -> i32 {
    match total {
        None | Some(0) => -1,
        Some(t) if received >= t => 99,
        Some(t) => ((100.0 * (received as f64 / t as f64)) as i32).min(99),
    }
}

/// HTTPS on the default port to an allow-listed GitHub host; no user info,
/// control characters or fragments.
pub fn allowed_url(url: &str) -> bool {
    let Some(rest) = url.strip_prefix("https://") else {
        return false;
    };
    if url
        .bytes()
        .any(|b| b < 0x21 || b == 0x7f || b == b'#' || b == b'\\')
    {
        return false;
    }
    let authority = rest.split(['/', '?']).next().unwrap_or("");
    if authority.contains('@') {
        return false;
    }
    let host = authority.strip_suffix(":443").unwrap_or(authority);
    HOSTS.contains(&host)
}

/// Resolves a redirect: root-relative paths stay on the current origin.
pub fn resolve_redirect(current: &str, location: &str) -> Option<String> {
    let next = if location.starts_with('/') && !location.starts_with("//") {
        let origin_end = current.get(8..)?.find('/').map_or(current.len(), |i| i + 8);
        format!("{}{location}", &current[..origin_end])
    } else {
        location.to_owned()
    };
    allowed_url(&next).then_some(next)
}

/// `sha256sum` output: `<64 hex>  <name>` or `<64 hex> *<name>`.
pub fn checksum_for(manifest: &str, asset: &str) -> Option<[u8; 32]> {
    manifest.lines().find_map(|line| {
        let (hex, name) = line.trim_end_matches('\r').split_once(' ')?;
        let name = name.trim_start_matches(' ');
        let name = name.strip_prefix('*').unwrap_or(name);
        if name != asset || hex.len() != 64 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
            return None;
        }
        let mut out = [0u8; 32];
        for (i, byte) in out.iter_mut().enumerate() {
            *byte = u8::from_str_radix(hex.get(i * 2..i * 2 + 2)?, 16).ok()?;
        }
        Some(out)
    })
}

pub fn sha256(bytes: &[u8]) -> [u8; 32] {
    use sha2::Digest;
    sha2::Sha256::digest(bytes).into()
}
