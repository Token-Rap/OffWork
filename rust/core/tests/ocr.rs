use offwork_core::ocr::clock_in;
#[test]
fn ctest_parser_cases() {
    assert_eq!(clock_in("上班打卡 09:12:34"), Some(552));
    assert_eq!(clock_in("07:01\n上班 09:12\n下班 18:00"), Some(552));
    assert_eq!(clock_in("上班 09:12\n下班打卡 06:00"), Some(552));
    assert_eq!(clock_in("打卡 08:30\n18:00"), Some(510));
    assert_eq!(clock_in("20:00 00:03 08:30"), Some(3));
    assert_eq!(clock_in("上 班 ０９ ： ０５ ： ４５"), Some(545));
    assert_eq!(clock_in("上班\n08:15"), Some(495));
    assert_eq!(clock_in("24:00 12:60 09:30:99 123:45 09:123 09:30:2"), None);
    assert_eq!(clock_in("没有时间"), None);
    assert_eq!(clock_in(""), None);
    assert_eq!(clock_in("23:59:59"), Some(1439));
    assert_eq!(clock_in("打卡结果\n08:55 上班打卡•成功\n班次时间 10月21日 08:30上班\n打卡方式 Wi-Fi打卡\n2026年10月21日"),Some(535));
    assert_eq!(
        clock_in("班次时间 10月21日 08:30上班\n打卡结果\n08:55 上班打卡•成功"),
        Some(535)
    );
    assert_eq!(
        clock_in("班次 08:30\n应上班 08:00\n打卡成功 08 ： 55"),
        Some(535)
    );
    assert_eq!(clock_in("打卡•成功 08：55"), Some(535));
    assert_eq!(clock_in("班次时间 08:30上班\n应上班 09:00"), None);
    assert_eq!(clock_in("班 次 时 间\n10 月 21 日 08 ： 30 上 班"), None);
    assert_eq!(clock_in("班次时间\n10月21日 08:30上班\n09:01"), Some(541));
    assert_eq!(
        clock_in("班次时间：\n08:30上班\n打卡成功\n08:55"),
        Some(535)
    );
    assert_eq!(
        clock_in("班次时间（计划）\n08:30上班\n打卡•成功\n08:55"),
        Some(535)
    );
    assert_eq!(clock_in("下班打卡成功 18:00\n上班打卡 08:55"), Some(535));
    assert_eq!(clock_in("下班打卡•成功 06:00\n上班 08:55"), Some(535));
    assert_eq!(clock_in("上班打卡 ０ ９ ： ０ ５"), Some(545));
    assert_eq!(clock_in("班次时间10月21日\n08:30上班\n08:55"), Some(535));
}
