//! Port of tests/update_tests.cpp plus checksum and redirect rules.
use offwork_core::update::*;

const SETUP: &str = "OffWork-Setup.exe";

#[test]
fn release_locations() {
    for location in [
        "https://github.com/Token-Rap/OffWork/releases/tag/v0.4.1",
        "/Token-Rap/OffWork/releases/tag/v0.4.1",
        "https://github.com/alertform/OffWork/releases/tag/v0.4.1",
        "/alertform/OffWork/releases/tag/v0.4.1",
    ] {
        let r = parse_release_location(location, SETUP).unwrap();
        assert_eq!(r.tag, "v0.4.1");
        assert_eq!(
            r.asset_url,
            "https://github.com/Token-Rap/OffWork/releases/download/v0.4.1/OffWork-Setup.exe"
        );
        assert_eq!(
            r.checksums_url,
            "https://github.com/Token-Rap/OffWork/releases/download/v0.4.1/SHA256SUMS"
        );
    }
    assert!(parse_release_location("/alertform/OffWork/releases/tag/v10.20.30", SETUP).is_some());
    for location in [
        "",
        "http://github.com/alertform/OffWork/releases/tag/v0.4.1",
        "https://github.com.evil.test/alertform/OffWork/releases/tag/v0.4.1",
        "https://evil.test/alertform/OffWork/releases/tag/v0.4.1",
        "https://github.com@evil.test/alertform/OffWork/releases/tag/v0.4.1",
        "//github.com/alertform/OffWork/releases/tag/v0.4.1",
        "/other/OffWork/releases/tag/v0.4.1",
        "/alertform/Other/releases/tag/v0.4.1",
        "/alertform/OffWork/releases/download/v0.4.1",
    ] {
        assert!(
            parse_release_location(location, SETUP).is_none(),
            "{location}"
        );
    }
    for tag in [
        "",
        "0.4.1",
        "V0.4.1",
        "v0.4",
        "v0.4.1.2",
        "v00.4.1",
        "v0.04.1",
        "v0.4.01",
        "v0.4.-1",
        "v0.4.1-rc.1",
        "v0.4.1+build.1",
        "v0.4.1/",
        "v0.4.1?x=1",
        "v0.4.1#fragment",
        "v0.4.1%2f..",
        "../v0.4.1",
        " v0.4.1",
        "v0.4.1 ",
        "v0.4.1\r\n",
        "v0.4.1\\evil",
        "v0.4.1\0evil",
    ] {
        let location = format!("/alertform/OffWork/releases/tag/{tag}");
        assert!(
            parse_release_location(&location, SETUP).is_none(),
            "{tag:?}"
        );
    }
}

#[test]
fn failed_checks_back_off_and_success_resets() {
    const MINUTE: u64 = 60_000;
    let mut schedule = CheckSchedule::default();
    let mut now = 12345;
    assert!(schedule.due(now));
    for minutes in [10, 20, 40, 80, 160, 320, 320, 320] {
        schedule = schedule.complete(now, false);
        assert!(!schedule.due(now) && !schedule.due(now + minutes * MINUTE - 1));
        assert!(schedule.due(now + minutes * MINUTE));
        now += minutes * MINUTE;
    }
    for _ in 0..1000 {
        schedule = schedule.complete(now, false);
    }
    assert!(!schedule.due(now + 320 * MINUTE - 1) && schedule.due(now + 320 * MINUTE));
    schedule = schedule.complete(now, true);
    assert!(!schedule.due(now + 360 * MINUTE - 1) && schedule.due(now + 360 * MINUTE));
    now += 360 * MINUTE;
    schedule = schedule.complete(now, false);
    assert!(!schedule.due(now + 10 * MINUTE - 1) && schedule.due(now + 10 * MINUTE));
}

#[test]
fn download_percent_and_versions() {
    for (received, total, out) in [
        (0, None, -1),
        (500, None, -1),
        (0, Some(0), -1),
        (0, Some(1000), 0),
        (1, Some(1000), 0),
        (500, Some(1000), 50),
        (999, Some(1000), 99),
        (1000, Some(1000), 99),
        (2000, Some(1000), 99),
        ((u64::MAX - 1) / 2, Some(u64::MAX - 1), 50),
        (u64::MAX, Some(u64::MAX), 99),
    ] {
        assert_eq!(download_percent(received, total), out);
    }
    assert!(is_newer("v0.4.0", "0.3.0"));
    assert!(is_newer("v0.10.0", "0.9.99"));
    assert!(is_newer("1.0.0", "0.99.99"));
    assert!(!is_newer("v0.4.0", "0.4.0"));
    assert!(!is_newer("v0.3.9", "0.4.0"));
    assert!(is_newer("1.0.0", "1.0.0-rc.1"));
    assert!(is_newer("1.0.0-rc.10", "1.0.0-rc.2"));
    assert!(!is_newer("1.0.0+build.2", "1.0.0+build.1"));
    for bad in [
        "",
        "v1.2",
        "1.2.3.4",
        "01.2.3",
        "1.2.-3",
        "1.2.3-01",
        "1.2.3+",
        "1.2.3-",
        "1.2.3 garbage",
    ] {
        assert!(!is_newer(bad, "0.1.0") && !is_newer("2.0.0", bad), "{bad}");
    }
    assert!(is_newer("999999999999999999999999.0.0", "2.0.0"));
    // The Rust preview must not be offered the legacy 0.4.5 stable build.
    assert!(!is_newer("v0.4.5", "0.5.0-preview.1"));
    assert!(is_newer("v0.5.0", "0.5.0-preview.1"));
}

fn sample() -> (String, String) {
    let url = "https://github.com/Token-Rap/OffWork/releases/download/v0.4.0/OffWork-Setup.exe"
        .to_string();
    let json = format!(
        r#"{{"body":"tag_name: fake, \"assets\": [] \u4e2d", "tag_name":"v0.4.0",
      "draft":false,"prerelease":false,"assets":[
      {{"name":"OffWork.exe","browser_download_url":"ignored"}},
      {{"browser_download_url":"{url}","name":"OffWork-Setup.exe"}}]}}"#
    );
    (url, json)
}

#[test]
fn release_json() {
    let (url, json) = sample();
    let r = parse_release_info(&json, SETUP).unwrap();
    assert!(r.tag == "v0.4.0" && r.asset_url == url);
    let legacy = json.replacen("Token-Rap", "alertform", 1);
    assert_eq!(
        parse_release_info(&legacy, SETUP).unwrap().asset_url,
        "https://github.com/alertform/OffWork/releases/download/v0.4.0/OffWork-Setup.exe"
    );
    let evil = json.replacen("github.com/", "github.com.evil.test/", 1);
    assert!(parse_release_info(&evil, SETUP).is_none());
    assert!(parse_release_info(r#"{"tag_name":"v0.4.0","assets":[]}"#, SETUP).is_none());
    assert!(parse_release_info(r#"{"tag_name":"v0.4.0"}"#, SETUP).is_none());
    assert!(parse_release_info(&(json.clone() + "garbage"), SETUP).is_none());
    assert!(parse_release_info(&json[..json.len() - 1], SETUP).is_none());
    for from in [
        "https://github.com/Token-Rap/OffWork/",
        "OffWork-Setup.exe",
        "v0.4.0",
    ] {
        assert!(
            parse_release_info(&json.replacen(from, "untrusted", 1), SETUP).is_none(),
            "{from}"
        );
    }
    for flag in ["draft", "prerelease"] {
        let changed = json.replace(&format!("\"{flag}\":false"), &format!("\"{flag}\":true"));
        assert!(parse_release_info(&changed, SETUP).is_none(), "{flag}");
    }
    // Duplicate keys are rejected, not "last one wins".
    let duplicate = json.replacen(
        "\"tag_name\":\"v0.4.0\"",
        "\"tag_name\":\"v0.4.0\",\"tag_name\":\"v9.0.0\"",
        1,
    );
    assert!(parse_release_info(&duplicate, SETUP).is_none());
    assert!(parse_release_info(&format!("{}{}", "[".repeat(80), "]".repeat(80)), SETUP).is_none());
    assert!(parse_release_info(&" ".repeat(MAX_JSON_BYTES + 1), SETUP).is_none());
    // Other platforms use their own exact asset name.
    let mac = json.replace("OffWork-Setup.exe", "OffWork-macOS-arm64.dmg");
    assert!(parse_release_info(&mac, "OffWork-macOS-arm64.dmg").is_some());
    assert!(parse_release_info(&mac, SETUP).is_none());
}

#[test]
fn urls_redirects_and_checksums() {
    for ok in [
        "https://github.com/Token-Rap/OffWork/releases/latest",
        "https://api.github.com/repos/Token-Rap/OffWork/releases/latest",
        "https://objects.githubusercontent.com/x?sig=1",
        "https://release-assets.githubusercontent.com:443/x",
    ] {
        assert!(allowed_url(ok), "{ok}");
    }
    for bad in [
        "http://github.com/",
        "https://github.com.evil.test/",
        "https://user@github.com/",
        "https://github.com:8443/",
        "https://evil.test/github.com",
        "https://github.com/a#b",
        "https://github.com/a\r\nX: y",
        "https://github.com/a b",
        "ftp://github.com/",
    ] {
        assert!(!allowed_url(bad), "{bad}");
    }
    assert_eq!(
        resolve_redirect(
            "https://github.com/Token-Rap/OffWork/releases/latest",
            "/Token-Rap/OffWork/releases/tag/v1.0.0"
        )
        .as_deref(),
        Some("https://github.com/Token-Rap/OffWork/releases/tag/v1.0.0")
    );
    assert!(resolve_redirect("https://github.com/x", "//evil.test/x").is_none());
    assert!(resolve_redirect("https://github.com/x", "https://evil.test/x").is_none());
    assert!(resolve_redirect(
        "https://github.com/x",
        "https://objects.githubusercontent.com/y"
    )
    .is_some());

    let digest = sha256(b"abc");
    let hex: String = digest.iter().map(|b| format!("{b:02x}")).collect();
    assert_eq!(
        hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    let manifest = format!(
        "{}  OffWork.exe\n{hex} *OffWork-Setup.exe\r\n",
        "0".repeat(64)
    );
    assert_eq!(checksum_for(&manifest, SETUP), Some(digest));
    assert_eq!(checksum_for(&manifest, "OffWork.exe"), Some([0; 32]));
    assert_eq!(checksum_for(&manifest, "missing"), None);
    assert_eq!(
        checksum_for(&format!("{}  {SETUP}", "+f".repeat(32)), SETUP),
        None
    );
}
