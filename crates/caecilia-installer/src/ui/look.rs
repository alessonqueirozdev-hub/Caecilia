//! The console's materials, by day and by night: the same colours as
//! `crates/caecilia-plugin/src/ui/look.rs`, kept here until the console's
//! materials move into a crate both can share.

use manguebyte_gfx::Color;
use manguebyte_ui::Theme;

/// The materials, in one light.
#[derive(Clone, Copy, Debug)]
pub struct Look {
    /// Night or day.
    pub night: bool,
    /// Behind everything.
    pub ground: Color,
    /// The wood, top and bottom of its gradient, and its grain.
    pub wood_top: Color,
    /// See [`wood_top`](Look::wood_top).
    pub wood_bottom: Color,
    /// See [`wood_top`](Look::wood_top).
    pub grain: Color,
    /// The panel: leather by night, paper by day.
    pub panel_top: Color,
    /// See [`panel_top`](Look::panel_top).
    pub panel_bottom: Color,
    /// A raised card on the panel.
    pub raised: Color,
    /// A field sunk into the panel.
    pub field: Color,
    /// Hairlines.
    pub edge: Color,
    /// The light along a bevel.
    pub sheen: Color,
    /// Ebony.
    pub ebony: Color,
    /// See [`ebony`](Look::ebony).
    pub ebony_light: Color,
    /// Ivory, light to shaded.
    pub ivory_light: Color,
    /// See [`ivory_light`](Look::ivory_light).
    pub ivory: Color,
    /// See [`ivory_light`](Look::ivory_light).
    pub ivory_shade: Color,
    /// Ink on ivory.
    pub ink: Color,
    /// Brass, light to dark.
    pub brass_light: Color,
    /// See [`brass_light`](Look::brass_light).
    pub brass: Color,
    /// See [`brass_light`](Look::brass_light).
    pub brass_dark: Color,
    /// Ink engraved in brass.
    pub plate_ink: Color,
    /// Text on the panel.
    pub text: Color,
    /// Quieter text.
    pub text2: Color,
    /// Quietest text.
    pub muted: Color,
    /// The accent.
    pub accent: Color,
    /// Text on the wood.
    pub on_wood: Color,
    /// A dial's face and its ink.
    pub dial: Color,
    /// See [`dial`](Look::dial).
    pub dial_ink: Color,
    /// Done, well.
    pub good: Color,
    /// The red of set and of removal.
    pub set_red: Color,
}

/// The night's light.
pub const NIGHT: Look = Look {
    night: true,
    ground: Color::hex(0x0D0A08),
    wood_top: Color::hex(0x2B1B13),
    wood_bottom: Color::hex(0x1A100B),
    grain: Color::hex_rgba(0x0000_0030),
    panel_top: Color::hex(0x211A15),
    panel_bottom: Color::hex(0x17120F),
    raised: Color::hex(0x2A2019),
    field: Color::hex(0x120E0B),
    edge: Color::hex(0x3A2D23),
    sheen: Color::hex_rgba(0xFFF0_D814),
    ebony: Color::hex(0x120D0A),
    ebony_light: Color::hex(0x3A2E26),
    ivory_light: Color::hex(0xFFFBF0),
    ivory: Color::hex(0xEFE5CF),
    ivory_shade: Color::hex(0xC9B994),
    ink: Color::hex(0x1C1510),
    brass_light: Color::hex(0xF6DA95),
    brass: Color::hex(0xCFA24E),
    brass_dark: Color::hex(0x8A6326),
    plate_ink: Color::hex(0x2B1D0C),
    text: Color::hex(0xF1E6D2),
    text2: Color::hex(0xC4B294),
    muted: Color::hex(0x8C7B65),
    accent: Color::hex(0xE0AE55),
    on_wood: Color::hex(0xC4B294),
    dial: Color::hex(0x16110D),
    dial_ink: Color::hex(0xE9DDC6),
    good: Color::hex(0x7FB77E),
    set_red: Color::hex(0xB33A2C),
};

/// The day's light: the installer's own, unless the person turns it.
pub const DAY: Look = Look {
    night: false,
    ground: Color::hex(0xEDE4D3),
    wood_top: Color::hex(0xD7BE95),
    wood_bottom: Color::hex(0xC3A273),
    grain: Color::hex_rgba(0x5A3A_1A22),
    panel_top: Color::hex(0xFBF6EC),
    panel_bottom: Color::hex(0xF3EBDC),
    raised: Color::hex(0xFFFDF8),
    field: Color::hex(0xFFFDF8),
    edge: Color::hex(0xD9CAB0),
    sheen: Color::hex_rgba(0xFFFF_FF99),
    ebony: Color::hex(0x1A130F),
    ebony_light: Color::hex(0x4A3B30),
    ivory_light: Color::hex(0xFFFDF6),
    ivory: Color::hex(0xF6EEDC),
    ivory_shade: Color::hex(0xD4C29E),
    ink: Color::hex(0x1C1510),
    brass_light: Color::hex(0xF4D58C),
    brass: Color::hex(0xC0913E),
    brass_dark: Color::hex(0x7E5A20),
    plate_ink: Color::hex(0x2B1D0C),
    text: Color::hex(0x2A1E14),
    text2: Color::hex(0x5E4B36),
    muted: Color::hex(0x7A6650),
    accent: Color::hex(0xA8742A),
    on_wood: Color::hex(0x3B2810),
    dial: Color::hex(0xFBF6EA),
    dial_ink: Color::hex(0x2A1E14),
    good: Color::hex(0x3F7A3E),
    set_red: Color::hex(0xB33A2C),
};

/// The toolkit's theme in these materials: focus rings and the like.
pub fn theme(look: &Look) -> Theme {
    let mut theme = if look.night {
        Theme::dark()
    } else {
        Theme::light()
    };
    let p = &mut theme.palette;
    p.background = look.ground;
    p.surface = look.panel_bottom;
    p.raised = look.raised;
    p.hairline = look.edge;
    p.text = look.text;
    p.text_secondary = look.text2;
    p.text_muted = look.muted;
    p.accent = look.accent;
    p.accent_bright = look.brass_light;
    p.accent_soft = look.accent.with_alpha(0x26);
    p.on_accent = look.plate_ink;
    p.focus = look.accent;
    theme
}
