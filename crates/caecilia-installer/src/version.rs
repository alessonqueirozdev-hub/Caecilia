//! Comparing versions such as `2.0.0-alpha.0`, as semantic versioning
//! orders them: by the numbers, and a pre-release before its release.

use std::cmp::Ordering;

/// How `a` compares with `b`.
pub fn compare(a: &str, b: &str) -> Ordering {
    let split = |v: &str| -> (Vec<u64>, Option<String>) {
        let v = v.trim().trim_start_matches(['v', 'V']);
        let v = v.split('+').next().unwrap_or(v);
        let (core, pre) = match v.split_once('-') {
            Some((c, p)) => (c, Some(p.to_owned())),
            None => (v, None),
        };
        (core.split('.').map(|n| n.parse().unwrap_or(0)).collect(), pre)
    };
    let (ca, pa) = split(a);
    let (cb, pb) = split(b);
    let len = ca.len().max(cb.len());
    for i in 0..len {
        let (x, y) = (ca.get(i).copied().unwrap_or(0), cb.get(i).copied().unwrap_or(0));
        if x != y {
            return x.cmp(&y);
        }
    }
    match (pa, pb) {
        (None, None) => Ordering::Equal,
        (None, Some(_)) => Ordering::Greater,
        (Some(_), None) => Ordering::Less,
        (Some(x), Some(y)) => {
            let xs: Vec<&str> = x.split('.').collect();
            let ys: Vec<&str> = y.split('.').collect();
            for (p, q) in xs.iter().zip(&ys) {
                let o = match (p.parse::<u64>(), q.parse::<u64>()) {
                    (Ok(m), Ok(n)) => m.cmp(&n),
                    (Ok(_), Err(_)) => Ordering::Less,
                    (Err(_), Ok(_)) => Ordering::Greater,
                    _ => p.cmp(q),
                };
                if o != Ordering::Equal {
                    return o;
                }
            }
            xs.len().cmp(&ys.len())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_order_as_semver_says() {
        let order = [
            "1.9.9",
            "2.0.0-alpha.0",
            "2.0.0-alpha.1",
            "2.0.0-alpha.10",
            "2.0.0-beta",
            "2.0.0-rc.1",
            "2.0.0",
            "2.0.1",
            "2.1",
            "10.0.0",
        ];
        for pair in order.windows(2) {
            assert_eq!(compare(pair[0], pair[1]), Ordering::Less, "{pair:?}");
            assert_eq!(compare(pair[1], pair[0]), Ordering::Greater, "{pair:?}");
        }
        assert_eq!(compare("2.0", "2.0.0"), Ordering::Equal);
        assert_eq!(compare("v2.0.0+build.5", "2.0.0"), Ordering::Equal);
    }
}
