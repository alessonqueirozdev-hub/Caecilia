//! The console's materials: wood, ivory, ebony, brass, in the night's
//! light and the day's, and the toolkit's theme dressed in them.

use manguebyte_gfx::Color;
use manguebyte_ui::Theme;

/// The materials of the console, in one light.
#[derive(Clone, Copy)]
pub(crate) struct Look {
    pub(crate) night: bool,
    pub(crate) ground: Color,
    pub(crate) wood_top: Color,
    pub(crate) wood_bottom: Color,
    pub(crate) grain: Color,
    pub(crate) panel_top: Color,
    pub(crate) panel_bottom: Color,
    pub(crate) edge: Color,
    pub(crate) sheen: Color,
    pub(crate) ebony: Color,
    pub(crate) ebony_light: Color,
    pub(crate) ivory_light: Color,
    pub(crate) ivory: Color,
    pub(crate) ivory_shade: Color,
    pub(crate) ink: Color,
    pub(crate) reed_ink: Color,
    pub(crate) brass_light: Color,
    pub(crate) brass: Color,
    pub(crate) brass_dark: Color,
    pub(crate) plate_ink: Color,
    pub(crate) text: Color,
    pub(crate) text2: Color,
    pub(crate) muted: Color,
    pub(crate) accent: Color,
    pub(crate) dial: Color,
    pub(crate) dial_ink: Color,
    pub(crate) good: Color,
    pub(crate) set_red: Color,
    /// The accent where it is written on the wood.
    pub(crate) wood_accent: Color,
}

pub(crate) const NIGHT: Look = Look {
    night: true,
    ground: Color::hex(0x0D0A08),
    wood_top: Color::hex(0x2B1B13),
    wood_bottom: Color::hex(0x1A100B),
    grain: Color::hex_rgba(0x0000_0030),
    panel_top: Color::hex(0x211A15),
    panel_bottom: Color::hex(0x17120F),
    edge: Color::hex(0x3A2D23),
    sheen: Color::hex_rgba(0xFFF0_D814),
    ebony: Color::hex(0x120D0A),
    ebony_light: Color::hex(0x3A2E26),
    ivory_light: Color::hex(0xFFFBF0),
    ivory: Color::hex(0xEFE5CF),
    ivory_shade: Color::hex(0xC9B994),
    ink: Color::hex(0x1C1510),
    reed_ink: Color::hex(0xA3261B),
    brass_light: Color::hex(0xF6DA95),
    brass: Color::hex(0xCFA24E),
    brass_dark: Color::hex(0x8A6326),
    plate_ink: Color::hex(0x2B1D0C),
    text: Color::hex(0xF1E6D2),
    text2: Color::hex(0xC4B294),
    muted: Color::hex(0x8C7B65),
    accent: Color::hex(0xE0AE55),
    dial: Color::hex(0x16110D),
    dial_ink: Color::hex(0xE9DDC6),
    good: Color::hex(0x7FB77E),
    set_red: Color::hex(0xB33A2C),
    wood_accent: Color::hex(0xE0AE55),
};

/// The day's light.
pub(crate) const DAY: Look = Look {
    night: false,
    ground: Color::hex(0xEDE4D3),
    wood_top: Color::hex(0xD7BE95),
    wood_bottom: Color::hex(0xC3A273),
    grain: Color::hex_rgba(0x5A3A_1A22),
    panel_top: Color::hex(0xFBF6EC),
    panel_bottom: Color::hex(0xF3EBDC),
    edge: Color::hex(0xD9CAB0),
    sheen: Color::hex_rgba(0xFFFF_FF99),
    ebony: Color::hex(0x1A130F),
    ebony_light: Color::hex(0x4A3B30),
    ivory_light: Color::hex(0xFFFDF6),
    ivory: Color::hex(0xF6EEDC),
    ivory_shade: Color::hex(0xD4C29E),
    ink: Color::hex(0x1C1510),
    reed_ink: Color::hex(0xA3261B),
    brass_light: Color::hex(0xF4D58C),
    brass: Color::hex(0xC0913E),
    brass_dark: Color::hex(0x7E5A20),
    plate_ink: Color::hex(0x2B1D0C),
    text: Color::hex(0x2A1E14),
    text2: Color::hex(0x5E4B36),
    muted: Color::hex(0x8E7B63),
    accent: Color::hex(0xA8742A),
    dial: Color::hex(0xFBF6EA),
    dial_ink: Color::hex(0x2A1E14),
    good: Color::hex(0x4E8A4D),
    set_red: Color::hex(0xB33A2C),
    wood_accent: Color::hex(0x5E3A10),
};

/// The toolkit's theme in the console's materials: pills, labels and the
/// keyboard take their colours from it.
pub(crate) fn theme(look: &Look) -> Theme {
    let mut theme = if look.night {
        Theme::dark()
    } else {
        Theme::light()
    };
    let p = &mut theme.palette;
    p.background = look.ground;
    p.surface = look.panel_bottom;
    p.raised = look.panel_top;
    p.hairline = look.edge;
    p.text = look.text;
    p.text_secondary = look.text2;
    p.text_muted = look.muted;
    p.accent = look.accent;
    p.accent_bright = look.brass_light;
    p.accent_soft = look.accent.with_alpha(0x26);
    p.on_accent = look.plate_ink;
    p.focus = look.accent;
    p.key_white = look.ivory;
    p.key_white_shade = look.ivory_shade;
    p.key_black = look.ebony;
    p.key_black_top = look.ebony_light;
    if look.night {
        p.raised = Color::hex(0x2A2019);
        p.overlay = Color::hex(0x372A20);
        p.hairline = Color::hex(0x3E3026);
    }
    theme
}
