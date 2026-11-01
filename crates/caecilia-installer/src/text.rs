//! The installer's words, in English, Brazilian Portuguese and Spanish,
//! chosen by the language of Windows. `{0}`, `{1}` are filled in with
//! [`fill`].

/// A language of the installer.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Lang {
    /// English.
    En,
    /// Brazilian Portuguese.
    Pt,
    /// Spanish.
    Es,
}

impl Lang {
    /// Every language, in the order the switch goes through them.
    pub const ALL: [Lang; 3] = [Lang::En, Lang::Pt, Lang::Es];

    /// Its code on the command line.
    pub fn code(self) -> &'static str {
        match self {
            Lang::En => "en",
            Lang::Pt => "pt",
            Lang::Es => "es",
        }
    }

    /// The language a code names (`pt`, `pt-BR`, `es-419`, …).
    pub fn from_code(code: &str) -> Option<Lang> {
        let primary = code.split(['-', '_']).next()?.to_ascii_lowercase();
        match primary.as_str() {
            "en" => Some(Lang::En),
            "pt" => Some(Lang::Pt),
            "es" => Some(Lang::Es),
            _ => None,
        }
    }

    /// The language for a Windows language identifier (`LANGID`): its
    /// primary language, else English.
    pub fn from_langid(langid: u16) -> Lang {
        match langid & 0x3FF {
            0x16 => Lang::Pt,
            0x0A => Lang::Es,
            _ => Lang::En,
        }
    }

    /// The words.
    pub fn text(self) -> &'static Text {
        match self {
            Lang::En => &EN,
            Lang::Pt => &PT,
            Lang::Es => &ES,
        }
    }
}

/// `template` with `{0}`, `{1}`, … replaced by `values`.
pub fn fill(template: &str, values: &[&str]) -> String {
    let mut out = template.to_owned();
    for (i, v) in values.iter().enumerate() {
        out = out.replace(&format!("{{{i}}}"), v);
    }
    out
}

/// Every string the installer shows.
#[derive(Debug)]
pub struct Text {
    /// The window's title: `{0}` the version.
    pub title: &'static str,
    /// The uninstaller's title.
    pub title_remove: &'static str,
    /// The licence line on the jamb.
    pub free: &'static str,
    /// The credits on the jamb and the last page.
    pub built_with: &'static str,
    /// The steps on the jamb.
    pub steps: [&'static str; 5],
    /// The step of a removal.
    pub step_remove: &'static str,

    /// Welcome.
    pub welcome_title: &'static str,
    /// Under it.
    pub welcome_lead: &'static str,
    /// Over the components.
    pub will_install: &'static str,
    /// For everyone, in a folder: `{0}`.
    pub for_everyone: &'static str,
    /// For the person, in a folder: `{0}`.
    pub for_me: &'static str,

    /// The components: name and what it is for.
    pub app: (&'static str, &'static str),
    /// See [`app`](Text::app).
    pub vst3: (&'static str, &'static str),
    /// See [`app`](Text::app).
    pub clap: (&'static str, &'static str),
    /// See [`app`](Text::app).
    pub organ: (&'static str, &'static str),

    /// The licence page.
    pub licence_title: &'static str,
    /// Under it.
    pub licence_lead: &'static str,
    /// The box to tick.
    pub licence_accept: &'static str,
    /// Said when Install is pressed before accepting.
    pub licence_needed: &'static str,

    /// Options.
    pub options_title: &'static str,
    /// Who for.
    pub who: &'static str,
    /// Everyone, and what that means.
    pub everyone: (&'static str, &'static str),
    /// Just me, and what that means.
    pub just_me: (&'static str, &'static str),
    /// The scope is that of the installation already there.
    pub scope_fixed: &'static str,
    /// What to install.
    pub what: &'static str,
    /// The program folder.
    pub folder: &'static str,
    /// Shortcuts.
    pub shortcuts: &'static str,
    /// On the desktop.
    pub desktop: &'static str,
    /// In the Start menu.
    pub start_menu: &'static str,
    /// The space needed: `{0}`.
    pub needs: &'static str,
    /// Nothing chosen.
    pub choose_something: &'static str,
    /// The title of the folder picker.
    pub pick_folder: &'static str,

    /// Buttons.
    pub next: &'static str,
    /// See [`next`](Text::next).
    pub back: &'static str,
    /// See [`next`](Text::next).
    pub options: &'static str,
    /// See [`next`](Text::next).
    pub install: &'static str,
    /// See [`next`](Text::next).
    pub cancel: &'static str,
    /// See [`next`](Text::next).
    pub finish: &'static str,
    /// See [`next`](Text::next).
    pub close: &'static str,
    /// See [`next`](Text::next).
    pub change: &'static str,
    /// See [`next`](Text::next).
    pub retry: &'static str,
    /// See [`next`](Text::next).
    pub remove: &'static str,
    /// See [`next`](Text::next).
    pub keep: &'static str,
    /// See [`next`](Text::next).
    pub show_log: &'static str,
    /// The switch of light: to night, to day.
    pub night_day: (&'static str, &'static str),
    /// The switch of language.
    pub language: &'static str,

    /// Installing.
    pub installing_title: &'static str,
    /// Removing.
    pub removing_title: &'static str,
    /// What each step does, by its name in the log.
    pub step_names: [(&'static str, &'static str); 11],
    /// Waiting for the administrator's permission.
    pub waiting_admin: &'static str,
    /// The fold of details.
    pub details: &'static str,
    /// The gauge's word.
    pub wind: &'static str,

    /// Done.
    pub done_title: &'static str,
    /// Under it.
    pub done_lead: &'static str,
    /// Open the app.
    pub open_now: &'static str,
    /// Rescan in the DAW.
    pub rescan: &'static str,
    /// Some old file goes at the next restart.
    pub restart_note: &'static str,

    /// It failed.
    pub failed_title: &'static str,
    /// Everything was undone.
    pub failed_lead: &'static str,
    /// The person cancelled.
    pub cancelled_lead: &'static str,
    /// UAC was declined.
    pub declined_lead: &'static str,
    /// Install just for me instead.
    pub just_me_instead: &'static str,
    /// The installer is damaged.
    pub damaged_title: &'static str,
    /// Under it.
    pub damaged_lead: &'static str,

    /// Already installed: `{0}` version, `{1}` who for.
    pub installed_title: &'static str,
    /// `{0}` installed version, `{1}` who for, `{2}` this one.
    pub installed_lead: &'static str,
    /// Update: `{0}` the version.
    pub update: (&'static str, &'static str),
    /// Repair.
    pub repair: (&'static str, &'static str),
    /// Change.
    pub modify: (&'static str, &'static str),
    /// Remove.
    pub uninstall: (&'static str, &'static str),
    /// A newer version is there: `{0}`.
    pub newer_lead: &'static str,
    /// "everyone on this PC" / "just you", in the sentences above.
    pub scope_words: (&'static str, &'static str),
    /// Caecilia 1.x is there.
    pub old_version: &'static str,

    /// The app is open.
    pub busy_title: &'static str,
    /// Under it.
    pub busy_lead: &'static str,

    /// Remove?
    pub remove_title: &'static str,
    /// Under it.
    pub remove_lead: &'static str,
    /// Purge.
    pub purge: (&'static str, &'static str),
    /// Removed.
    pub removed_title: &'static str,
    /// Under it.
    pub removed_lead: &'static str,
    /// Kept because changed.
    pub kept: &'static str,
}

/// English.
pub const EN: Text = Text {
    title: "Caecilia {0} Setup",
    title_remove: "Remove Caecilia",
    free: "Free software · Apache License 2.0",
    built_with: "Built in Rust with the Manguebyte Audio Framework",
    steps: ["Welcome", "Licence", "Options", "Installing", "Done"],
    step_remove: "Remove",
    welcome_title: "Caecilia, the pipe organ",
    welcome_lead: "An organ to play from your MIDI keyboard: an app of its own, and a VST3 and CLAP instrument in your DAW.",
    will_install: "This will install",
    for_everyone: "For everyone on this PC, in {0}.",
    for_me: "Just for you, in {0}.",
    app: ("Caecilia app", "standalone"),
    vst3: ("VST3 plugin", "for most DAWs"),
    clap: ("CLAP plugin", "for Bitwig, Reaper…"),
    organ: ("Demonstration organ", "in your Organs folder"),
    licence_title: "Licence",
    licence_lead: "Caecilia is free software under the Apache License 2.0. It comes with no warranty. Please read the terms.",
    licence_accept: "I have read and accept the terms of the licence",
    licence_needed: "Tick the box to accept the licence first.",
    options_title: "Options",
    who: "Who is it for?",
    everyone: ("Everyone on this PC", "Plugins go where every DAW looks. Asks for an administrator once."),
    just_me: ("Just me", "No administrator needed. Some older DAWs don't look in your own plugin folders."),
    scope_fixed: "As the installation already here: to change it, remove Caecilia first.",
    what: "What to install",
    folder: "Program folder",
    shortcuts: "Shortcuts",
    desktop: "Desktop",
    start_menu: "Start menu",
    needs: "Needs {0}",
    choose_something: "Choose at least one thing to install.",
    pick_folder: "Where should Caecilia go?",
    next: "Next",
    back: "Back",
    options: "Options…",
    install: "Install",
    cancel: "Cancel",
    finish: "Finish",
    close: "Close",
    change: "Change…",
    retry: "Retry",
    remove: "Remove",
    keep: "Keep it",
    show_log: "Show the log",
    night_day: ("Night", "Day"),
    language: "Language",
    installing_title: "Installing Caecilia",
    removing_title: "Removing Caecilia",
    step_names: [
        ("core", "Placing the licence and the icon…"),
        ("app", "Installing the Caecilia app…"),
        ("vst3", "Installing the VST3 plugin…"),
        ("clap", "Installing the CLAP plugin…"),
        ("organ", "Adding the demonstration organ…"),
        ("uninstaller", "Placing the uninstaller…"),
        ("shortcuts", "Making shortcuts…"),
        ("register", "Registering in Apps & features…"),
        ("files", "Removing the files…"),
        ("purge", "Removing your organs and settings…"),
        ("", "Getting ready…"),
    ],
    waiting_admin: "Waiting for the administrator's permission…",
    details: "Details",
    wind: "WIND",
    done_title: "Caecilia is installed",
    done_lead: "Everything is in place, and every file checked.",
    open_now: "Open Caecilia now",
    rescan: "In your DAW, rescan plugins: Caecilia appears among the instruments, as VST3 and as CLAP.",
    restart_note: "An old file in use is deleted when Windows next restarts.",
    failed_title: "Caecilia was not installed",
    failed_lead: "Nothing was changed: every step was undone.",
    cancelled_lead: "Cancelled. Nothing was changed: every step was undone.",
    declined_lead: "The administrator's permission was not given, so nothing was installed for everyone.",
    just_me_instead: "Install just for me",
    damaged_title: "This installer is damaged",
    damaged_lead: "Its files do not match their hashes. Download it again.",
    installed_title: "Caecilia is already installed",
    installed_lead: "Version {0} is here, for {1}. This installer has {2}.",
    update: ("Update to {0}", "Keeps your folder, components and organs."),
    repair: ("Repair", "Puts back any file that is missing or changed."),
    modify: ("Change", "Add or leave out components and shortcuts."),
    uninstall: ("Remove", "Uninstalls Caecilia from this PC."),
    newer_lead: "A newer version, {0}, is installed. Remove it first to install this one.",
    scope_words: ("everyone on this PC", "just you"),
    old_version: "Caecilia 1.x is installed too. Remove it from Apps & features when you are ready.",
    busy_title: "Caecilia is open",
    busy_lead: "Close Caecilia so its files can be updated, then choose Retry. Your session is saved when it closes.",
    remove_title: "Remove Caecilia?",
    remove_lead: "This removes what the installer put on this PC:",
    purge: ("Also remove my organs and settings", "Organs you added and your last session. Unticked, they stay for next time."),
    removed_title: "Caecilia was removed",
    removed_lead: "Everything the installer placed is gone.",
    kept: "Kept, because it was changed after it was installed:",
};

/// Brazilian Portuguese.
pub const PT: Text = Text {
    title: "Instalação do Caecilia {0}",
    title_remove: "Remover o Caecilia",
    free: "Software livre · Licença Apache 2.0",
    built_with: "Feito em Rust com o Manguebyte Audio Framework",
    steps: ["Boas-vindas", "Licença", "Opções", "Instalando", "Pronto"],
    step_remove: "Remover",
    welcome_title: "Caecilia, o órgão de tubos",
    welcome_lead: "Um órgão para tocar do seu teclado MIDI: um aplicativo próprio e um instrumento VST3 e CLAP na sua DAW.",
    will_install: "Será instalado",
    for_everyone: "Para todos neste PC, em {0}.",
    for_me: "Só para você, em {0}.",
    app: ("Aplicativo Caecilia", "standalone"),
    vst3: ("Plugin VST3", "para a maioria das DAWs"),
    clap: ("Plugin CLAP", "para Bitwig, Reaper…"),
    organ: ("Órgão de demonstração", "na sua pasta Organs"),
    licence_title: "Licença",
    licence_lead: "O Caecilia é software livre sob a Licença Apache 2.0. Ele vem sem garantia. Leia os termos.",
    licence_accept: "Li e aceito os termos da licença",
    licence_needed: "Marque a caixa para aceitar a licença primeiro.",
    options_title: "Opções",
    who: "Para quem?",
    everyone: ("Todos neste PC", "Os plugins vão para onde toda DAW procura. Pede administrador uma vez."),
    just_me: ("Só para mim", "Sem administrador. Algumas DAWs antigas não procuram nas pastas de plugins do usuário."),
    scope_fixed: "Como a instalação que já existe: para mudar, remova o Caecilia antes.",
    what: "O que instalar",
    folder: "Pasta do programa",
    shortcuts: "Atalhos",
    desktop: "Área de trabalho",
    start_menu: "Menu Iniciar",
    needs: "Precisa de {0}",
    choose_something: "Escolha pelo menos uma coisa para instalar.",
    pick_folder: "Onde o Caecilia deve ficar?",
    next: "Avançar",
    back: "Voltar",
    options: "Opções…",
    install: "Instalar",
    cancel: "Cancelar",
    finish: "Concluir",
    close: "Fechar",
    change: "Alterar…",
    retry: "Tentar de novo",
    remove: "Remover",
    keep: "Manter",
    show_log: "Ver o log",
    night_day: ("Noite", "Dia"),
    language: "Idioma",
    installing_title: "Instalando o Caecilia",
    removing_title: "Removendo o Caecilia",
    step_names: [
        ("core", "Colocando a licença e o ícone…"),
        ("app", "Instalando o aplicativo Caecilia…"),
        ("vst3", "Instalando o plugin VST3…"),
        ("clap", "Instalando o plugin CLAP…"),
        ("organ", "Adicionando o órgão de demonstração…"),
        ("uninstaller", "Colocando o desinstalador…"),
        ("shortcuts", "Criando atalhos…"),
        ("register", "Registrando em Aplicativos instalados…"),
        ("files", "Removendo os arquivos…"),
        ("purge", "Removendo seus órgãos e configurações…"),
        ("", "Preparando…"),
    ],
    waiting_admin: "Aguardando a permissão do administrador…",
    details: "Detalhes",
    wind: "VENTO",
    done_title: "O Caecilia está instalado",
    done_lead: "Tudo no lugar, e cada arquivo conferido.",
    open_now: "Abrir o Caecilia agora",
    rescan: "Na sua DAW, reescaneie os plugins: o Caecilia aparece entre os instrumentos, como VST3 e como CLAP.",
    restart_note: "Um arquivo antigo em uso será apagado quando o Windows reiniciar.",
    failed_title: "O Caecilia não foi instalado",
    failed_lead: "Nada foi alterado: cada passo foi desfeito.",
    cancelled_lead: "Cancelado. Nada foi alterado: cada passo foi desfeito.",
    declined_lead: "A permissão do administrador não foi dada, então nada foi instalado para todos.",
    just_me_instead: "Instalar só para mim",
    damaged_title: "Este instalador está danificado",
    damaged_lead: "Os arquivos dele não batem com os hashes. Baixe-o de novo.",
    installed_title: "O Caecilia já está instalado",
    installed_lead: "A versão {0} está aqui, para {1}. Este instalador traz a {2}.",
    update: ("Atualizar para {0}", "Mantém sua pasta, componentes e órgãos."),
    repair: ("Reparar", "Repõe qualquer arquivo que falte ou tenha mudado."),
    modify: ("Alterar", "Inclui ou deixa de fora componentes e atalhos."),
    uninstall: ("Remover", "Desinstala o Caecilia deste PC."),
    newer_lead: "Uma versão mais nova, {0}, está instalada. Remova-a antes para instalar esta.",
    scope_words: ("todos neste PC", "só você"),
    old_version: "O Caecilia 1.x também está instalado. Remova-o em Aplicativos instalados quando quiser.",
    busy_title: "O Caecilia está aberto",
    busy_lead: "Feche o Caecilia para que os arquivos dele possam ser atualizados e escolha Tentar de novo. Sua sessão é salva ao fechar.",
    remove_title: "Remover o Caecilia?",
    remove_lead: "Isto remove o que o instalador colocou neste PC:",
    purge: ("Remover também meus órgãos e configurações", "Órgãos que você adicionou e sua última sessão. Desmarcado, ficam para a próxima vez."),
    removed_title: "O Caecilia foi removido",
    removed_lead: "Tudo o que o instalador colocou foi retirado.",
    kept: "Mantido, porque foi alterado depois de instalado:",
};

/// Spanish.
pub const ES: Text = Text {
    title: "Instalación de Caecilia {0}",
    title_remove: "Quitar Caecilia",
    free: "Software libre · Licencia Apache 2.0",
    built_with: "Hecho en Rust con el Manguebyte Audio Framework",
    steps: ["Bienvenida", "Licencia", "Opciones", "Instalando", "Listo"],
    step_remove: "Quitar",
    welcome_title: "Caecilia, el órgano de tubos",
    welcome_lead: "Un órgano para tocar desde tu teclado MIDI: una aplicación propia y un instrumento VST3 y CLAP en tu DAW.",
    will_install: "Se instalará",
    for_everyone: "Para todos en este PC, en {0}.",
    for_me: "Solo para ti, en {0}.",
    app: ("Aplicación Caecilia", "independiente"),
    vst3: ("Plugin VST3", "para la mayoría de las DAW"),
    clap: ("Plugin CLAP", "para Bitwig, Reaper…"),
    organ: ("Órgano de demostración", "en tu carpeta Organs"),
    licence_title: "Licencia",
    licence_lead: "Caecilia es software libre bajo la Licencia Apache 2.0. Se ofrece sin garantía. Lee los términos.",
    licence_accept: "He leído y acepto los términos de la licencia",
    licence_needed: "Marca la casilla para aceptar la licencia primero.",
    options_title: "Opciones",
    who: "¿Para quién?",
    everyone: ("Todos en este PC", "Los plugins van donde toda DAW busca. Pide administrador una vez."),
    just_me: ("Solo para mí", "Sin administrador. Algunas DAW antiguas no buscan en las carpetas de plugins del usuario."),
    scope_fixed: "Como la instalación que ya existe: para cambiarlo, quita Caecilia antes.",
    what: "Qué instalar",
    folder: "Carpeta del programa",
    shortcuts: "Accesos directos",
    desktop: "Escritorio",
    start_menu: "Menú Inicio",
    needs: "Necesita {0}",
    choose_something: "Elige al menos una cosa para instalar.",
    pick_folder: "¿Dónde debe ir Caecilia?",
    next: "Siguiente",
    back: "Atrás",
    options: "Opciones…",
    install: "Instalar",
    cancel: "Cancelar",
    finish: "Finalizar",
    close: "Cerrar",
    change: "Cambiar…",
    retry: "Reintentar",
    remove: "Quitar",
    keep: "Mantener",
    show_log: "Ver el registro",
    night_day: ("Noche", "Día"),
    language: "Idioma",
    installing_title: "Instalando Caecilia",
    removing_title: "Quitando Caecilia",
    step_names: [
        ("core", "Colocando la licencia y el icono…"),
        ("app", "Instalando la aplicación Caecilia…"),
        ("vst3", "Instalando el plugin VST3…"),
        ("clap", "Instalando el plugin CLAP…"),
        ("organ", "Añadiendo el órgano de demostración…"),
        ("uninstaller", "Colocando el desinstalador…"),
        ("shortcuts", "Creando accesos directos…"),
        ("register", "Registrando en Aplicaciones instaladas…"),
        ("files", "Quitando los archivos…"),
        ("purge", "Quitando tus órganos y ajustes…"),
        ("", "Preparando…"),
    ],
    waiting_admin: "Esperando el permiso del administrador…",
    details: "Detalles",
    wind: "VIENTO",
    done_title: "Caecilia está instalado",
    done_lead: "Todo en su lugar, y cada archivo comprobado.",
    open_now: "Abrir Caecilia ahora",
    rescan: "En tu DAW, vuelve a escanear los plugins: Caecilia aparece entre los instrumentos, como VST3 y como CLAP.",
    restart_note: "Un archivo antiguo en uso se borrará cuando Windows se reinicie.",
    failed_title: "Caecilia no se instaló",
    failed_lead: "No se cambió nada: cada paso se deshizo.",
    cancelled_lead: "Cancelado. No se cambió nada: cada paso se deshizo.",
    declined_lead: "No se dio el permiso del administrador, así que no se instaló nada para todos.",
    just_me_instead: "Instalar solo para mí",
    damaged_title: "Este instalador está dañado",
    damaged_lead: "Sus archivos no coinciden con sus hashes. Descárgalo de nuevo.",
    installed_title: "Caecilia ya está instalado",
    installed_lead: "La versión {0} está aquí, para {1}. Este instalador trae la {2}.",
    update: ("Actualizar a {0}", "Mantiene tu carpeta, componentes y órganos."),
    repair: ("Reparar", "Repone cualquier archivo que falte o haya cambiado."),
    modify: ("Cambiar", "Incluye o deja fuera componentes y accesos directos."),
    uninstall: ("Quitar", "Desinstala Caecilia de este PC."),
    newer_lead: "Hay una versión más nueva, {0}, instalada. Quítala antes para instalar esta.",
    scope_words: ("todos en este PC", "solo tú"),
    old_version: "Caecilia 1.x también está instalado. Quítalo en Aplicaciones instaladas cuando quieras.",
    busy_title: "Caecilia está abierto",
    busy_lead: "Cierra Caecilia para que sus archivos se puedan actualizar y elige Reintentar. Tu sesión se guarda al cerrar.",
    remove_title: "¿Quitar Caecilia?",
    remove_lead: "Esto quita lo que el instalador puso en este PC:",
    purge: ("Quitar también mis órganos y ajustes", "Órganos que añadiste y tu última sesión. Sin marcar, se quedan para la próxima vez."),
    removed_title: "Caecilia se quitó",
    removed_lead: "Todo lo que el instalador colocó ya no está.",
    kept: "Se mantuvo, porque se cambió después de instalarlo:",
};

/// A size in bytes, for people: `14 MB`.
pub fn size(bytes: u64) -> String {
    const MB: u64 = 1 << 20;
    if bytes >= MB {
        format!("{} MB", bytes.div_ceil(MB))
    } else {
        format!("{} KB", bytes.div_ceil(1024))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn languages_come_from_windows_and_codes() {
        assert_eq!(Lang::from_langid(0x0416), Lang::Pt); // pt-BR
        assert_eq!(Lang::from_langid(0x0816), Lang::Pt); // pt-PT
        assert_eq!(Lang::from_langid(0x0C0A), Lang::Es); // es-ES
        assert_eq!(Lang::from_langid(0x080A), Lang::Es); // es-MX
        assert_eq!(Lang::from_langid(0x0407), Lang::En); // de-DE
        assert_eq!(Lang::from_code("pt-BR"), Some(Lang::Pt));
        assert_eq!(Lang::from_code("ES"), Some(Lang::Es));
        assert_eq!(Lang::from_code("fr"), None);
    }

    #[test]
    fn templates_fill_in() {
        assert_eq!(fill(EN.title, &["2.0.0"]), "Caecilia 2.0.0 Setup");
        assert_eq!(fill(PT.installed_lead, &["1", "você", "2"]), "A versão 1 está aqui, para você. Este instalador traz a 2.");
        assert_eq!(size(14 << 20), "14 MB");
        assert_eq!(size(1000), "1 KB");
    }

    #[test]
    fn every_language_names_every_step() {
        for lang in Lang::ALL {
            let names: Vec<_> = lang.text().step_names.iter().map(|(k, _)| *k).collect();
            assert_eq!(names, EN.step_names.iter().map(|(k, _)| *k).collect::<Vec<_>>());
        }
    }
}
