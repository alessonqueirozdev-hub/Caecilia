//! The selector grammar: one text form shared by the console, MIDI learn,
//! OSC and JSON-RPC, so a stop is selected the same way everywhere.
//!
//! ```text
//! union     := intersect ( '|' intersect )*
//! intersect := unary ( ( '&' | '-' ) unary )*
//! unary     := '!' unary | primary
//! primary   := '(' union ')' | 'engaged' | 'all' | '*' | key ':' value
//! key       := id | family | role | class | pitch | footage | div | division | name
//! ```
//!
//! For example `family:reed & pitch:8 & div:swell`, or `div:great - pitch:2 2/3`
//! spelled `div:great - pitch:8/3`. Values may be quoted: `name:"voix céleste"`.

use caecilia_model::{ChorusRole, Footage, PitchClass, StopId, TonalFamily};

use crate::selector::{Engaged, Selector, StopQuery};

/// The grammar's version: a public contract external scripts depend on. Bump
/// the minor for additions, the major for anything that changes a meaning.
pub const GRAMMAR_VERSION: (u16, u16) = (1, 0);

/// A selector that did not parse.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParseError {
    /// What was wrong.
    pub message: String,
    /// Byte offset in the source.
    pub position: usize,
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} (at {})", self.message, self.position)
    }
}

impl std::error::Error for ParseError {}

#[derive(Clone, Debug, PartialEq)]
enum Tok {
    LParen,
    RParen,
    Amp,
    Pipe,
    Bang,
    Dash,
    Colon,
    Word(String),
    End,
}

fn is_word_char(c: char) -> bool {
    c.is_alphanumeric() || matches!(c, '_' | '.' | '/' | '\'')
}

fn tokenize(text: &str) -> Vec<(Tok, usize)> {
    let mut out = Vec::new();
    let mut chars = text.char_indices().peekable();
    while let Some(&(i, c)) = chars.peek() {
        if c.is_whitespace() {
            chars.next();
            continue;
        }
        let single = match c {
            '(' => Some(Tok::LParen),
            ')' => Some(Tok::RParen),
            '&' => Some(Tok::Amp),
            '|' => Some(Tok::Pipe),
            '!' => Some(Tok::Bang),
            '-' => Some(Tok::Dash),
            ':' => Some(Tok::Colon),
            '*' => Some(Tok::Word("*".into())),
            _ => None,
        };
        if let Some(t) = single {
            out.push((t, i));
            chars.next();
            continue;
        }
        if c == '"' {
            chars.next();
            let mut v = String::new();
            for (_, ch) in chars.by_ref() {
                if ch == '"' {
                    break;
                }
                v.push(ch);
            }
            out.push((Tok::Word(v), i));
            continue;
        }
        if is_word_char(c) {
            let mut w = String::new();
            while let Some(&(_, ch)) = chars.peek() {
                if !is_word_char(ch) {
                    break;
                }
                w.push(ch);
                chars.next();
            }
            out.push((Tok::Word(w), i));
            continue;
        }
        // An unknown character becomes a word, so the parser can name it.
        out.push((Tok::Word(c.to_string()), i));
        chars.next();
    }
    out.push((Tok::End, text.len()));
    out
}

fn parse_family(v: &str) -> Option<TonalFamily> {
    Some(match v.to_lowercase().as_str() {
        "principal" | "diapason" => TonalFamily::Principal,
        "flute" => TonalFamily::Flute,
        "string" => TonalFamily::String,
        "reed" => TonalFamily::Reed,
        "mixture" => TonalFamily::Mixture,
        "hybrid" => TonalFamily::Hybrid,
        "percussion" => TonalFamily::Percussion,
        _ => return None,
    })
}

fn parse_role(v: &str) -> Option<ChorusRole> {
    Some(match v.to_lowercase().as_str() {
        "foundation" => ChorusRole::Foundation,
        "chorus" => ChorusRole::Chorus,
        "crown" | "mixturecrown" => ChorusRole::MixtureCrown,
        "solo" => ChorusRole::Solo,
        "color" | "colour" => ChorusRole::Color,
        "effect" => ChorusRole::Effect,
        _ => return None,
    })
}

fn parse_class(v: &str) -> Option<PitchClass> {
    Some(match v.to_lowercase().as_str() {
        "sub" => PitchClass::Sub,
        "unison" => PitchClass::Unison,
        "octave" => PitchClass::Octave,
        "super" => PitchClass::Super,
        "mutation" => PitchClass::Mutation,
        "compound" => PitchClass::Compound,
        _ => return None,
    })
}

fn parse_footage(v: &str) -> Option<Footage> {
    match v.split_once('/') {
        None => v
            .parse::<i32>()
            .ok()
            .filter(|n| *n > 0)
            .map(|n| Footage::new(n, 1)),
        Some((n, d)) => {
            let (n, d) = (n.parse::<i32>().ok()?, d.parse::<i32>().ok()?);
            (n > 0 && d > 0).then(|| Footage::new(n, d))
        }
    }
}

struct Parser {
    tokens: Vec<(Tok, usize)>,
    at: usize,
}

type Parsed = Result<Selector, ParseError>;

impl Parser {
    fn peek(&self) -> &Tok {
        &self.tokens[self.at].0
    }

    fn advance(&mut self) -> Tok {
        let t = self.tokens[self.at].0.clone();
        if self.at < self.tokens.len() - 1 {
            self.at += 1;
        }
        t
    }

    fn error(&self, message: impl Into<String>) -> ParseError {
        ParseError {
            message: message.into(),
            position: self.tokens[self.at].1,
        }
    }

    fn union(&mut self) -> Parsed {
        let mut lhs = self.intersection()?;
        while *self.peek() == Tok::Pipe {
            self.advance();
            lhs = Selector::Union(vec![lhs, self.intersection()?]);
        }
        Ok(lhs)
    }

    fn intersection(&mut self) -> Parsed {
        let mut lhs = self.unary()?;
        loop {
            match self.peek() {
                Tok::Amp => {
                    self.advance();
                    lhs = Selector::Intersect(vec![lhs, self.unary()?]);
                }
                Tok::Dash => {
                    self.advance();
                    lhs = Selector::Difference(vec![lhs, self.unary()?]);
                }
                _ => return Ok(lhs),
            }
        }
    }

    fn unary(&mut self) -> Parsed {
        if *self.peek() == Tok::Bang {
            self.advance();
            return Ok(!self.unary()?);
        }
        self.primary()
    }

    fn primary(&mut self) -> Parsed {
        match self.peek().clone() {
            Tok::LParen => {
                self.advance();
                let inner = self.union()?;
                if *self.peek() != Tok::RParen {
                    return Err(self.error("expected ')'"));
                }
                self.advance();
                Ok(inner)
            }
            Tok::Word(key) => {
                let key_at = self.at;
                self.advance();
                if *self.peek() != Tok::Colon {
                    return match key.to_lowercase().as_str() {
                        "engaged" => Ok(Selector::Atom(StopQuery {
                            engaged: Engaged::Only,
                            ..Default::default()
                        })),
                        "all" | "*" => Ok(Selector::default()),
                        _ => Err(ParseError {
                            message: format!("unknown keyword '{key}'"),
                            position: self.tokens[key_at].1,
                        }),
                    };
                }
                self.advance();
                let Tok::Word(value) = self.peek().clone() else {
                    return Err(self.error("expected value after ':'"));
                };
                let value_at = self.at;
                self.advance();
                self.predicate(&key, &value, self.tokens[value_at].1)
            }
            _ => Err(self.error("expected a selector term")),
        }
    }

    fn predicate(&self, key: &str, value: &str, at: usize) -> Parsed {
        let err = |m: String| ParseError {
            message: m,
            position: at,
        };
        let mut q = StopQuery::default();
        match key.to_lowercase().as_str() {
            "id" => {
                // A stop id that does not parse is a typo to hear about, not a
                // query that silently matches everything.
                let ok = !value.is_empty()
                    && value.len() <= 5
                    && value.bytes().all(|b| b.is_ascii_digit());
                let id = value.parse::<u16>().ok().filter(|_| ok);
                q.id = Some(StopId(id.ok_or_else(|| {
                    err(format!("expected a stop id after 'id:', got '{value}'"))
                })?));
            }
            "family" => {
                q.family = Some(
                    parse_family(value).ok_or_else(|| err(format!("unknown family '{value}'")))?,
                )
            }
            "role" => {
                q.role =
                    Some(parse_role(value).ok_or_else(|| err(format!("unknown role '{value}'")))?)
            }
            "class" => {
                q.pitch_class = Some(
                    parse_class(value)
                        .ok_or_else(|| err(format!("unknown pitch class '{value}'")))?,
                )
            }
            "pitch" | "footage" => {
                q.footage = Some(
                    parse_footage(value)
                        .ok_or_else(|| err(format!("invalid footage '{value}'")))?,
                )
            }
            "div" | "division" => q.division_name = value.into(),
            "name" => q.name_contains = value.into(),
            _ => return Err(err(format!("unknown key '{key}'"))),
        }
        Ok(Selector::Atom(q))
    }
}

/// Parse a selector. Empty text selects every stop.
pub fn parse(text: &str) -> Result<Selector, ParseError> {
    let mut p = Parser {
        tokens: tokenize(text),
        at: 0,
    };
    if *p.peek() == Tok::End {
        return Ok(Selector::default());
    }
    let s = p.union()?;
    if *p.peek() != Tok::End {
        return Err(p.error("unexpected trailing input"));
    }
    Ok(s)
}
