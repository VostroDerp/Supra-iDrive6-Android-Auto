#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds NbtAad_ALL_Texts.zip (<lang>.txt with "id=text" lines, same format as BMW's PluginsCoded apps)
# and NbtAad_ALL_Images.zip (<imageId>.png).
import os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol", "es-US": "Espanol (US)",
         "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian"}
ABOUT = ("NBT-AAD: native wireless Android Auto\\nDeveloped by nanaisu\\n"
         "With contributions from @houssems, Tom Pryor (@tom-pryor) and @dn00\\n"
         "github.com/Sq00ky/Supra-iDrive6-Android-Auto\\n"
         "Free of charge. If you paid for it, you were scammed.")
TEXTS = {1: "Android Auto", 2: "Android Auto: Connecting...",   # 2 = entry text while nbt-aad is connecting
         # Android Auto menu (hmiState 10): row 1 changes with the daemon state (10 / 13 / 14)
         10: "Start Android Auto", 11: "Force Stop", 12: "Restart", 13: "Connecting...", 14: "Open Android Auto",
         # 15 = "start at boot" checkbox, 16 = About row / page title, 17 = About page text (hmiState 12): a Richtext
         # list wraps it to the screen width; "\n" (backslash + n in the .txt) is a line break, as in BMW's own
         # ActiveSeating text 88. English only.
         15: "Start automatically", 16: "About", 18: "Save logs to USB",   # 18 = log checkbox (rhmiapp-10)
         19: "MENU key opens iDrive",   # 19 = MENU key checkbox (rhmiapp-14)
         20: "Show driving mode changes",   # 20 = driving mode checkbox (rhmiapp-21)
         21: "Turn arrows in cluster/HUD",   # 21 = cluster/HUD arrows checkbox (kombi_nav)
         22: "Show iDrive warnings",   # 22 = iDrive warnings checkbox (rhmiapp-32)
         23: "Now playing",   # 23 = now playing row + screen title (NbtAad_Media.xml only)
         17: ABOUT}
# Per-language overrides (ASCII only, like the language names above).
LANG_TEXTS = {
    "de": {23: "Aktuelle Wiedergabe", 22: "iDrive-Warnungen anzeigen", 21: "Abbiegepfeile in Kombi/HUD", 20: "Fahrmodus-Wechsel anzeigen", 19: "MENU-Taste oeffnet iDrive", 2: "Android Auto: Verbinden...", 18: "Protokolle auf USB speichern", 10: "Android Auto starten", 11: "Beenden erzwingen", 12: "Neu starten",
           13: "Verbinden...", 14: "Android Auto oeffnen", 15: "Automatisch starten", 16: "Info"},
    "es-ES": {23: "Reproduciendo", 22: "Mostrar avisos de iDrive", 21: "Flechas de giro en cuadro/HUD", 20: "Mostrar cambios de modo de conduccion", 19: "La tecla MENU abre iDrive", 2: "Android Auto: Conectando...", 18: "Guardar registros en USB", 10: "Iniciar Android Auto", 11: "Forzar cierre", 12: "Reiniciar",
              13: "Conectando...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Acerca de"},
    "fr-FR": {23: "Lecture en cours", 22: "Afficher les avertissements iDrive", 21: "Fleches de direction combine/HUD", 20: "Afficher les changements de mode de conduite", 19: "La touche MENU ouvre iDrive", 2: "Android Auto : Connexion...", 18: "Enregistrer les journaux sur USB", 10: "Demarrer Android Auto", 11: "Forcer l'arret", 12: "Redemarrer",
              13: "Connexion...", 14: "Ouvrir Android Auto", 15: "Demarrage automatique", 16: "A propos"},
    "it": {23: "In riproduzione", 22: "Mostra gli avvisi di iDrive", 21: "Frecce di svolta nel quadro/HUD", 20: "Mostra i cambi di modalita di guida", 19: "Il tasto MENU apre iDrive", 2: "Android Auto: Connessione...", 18: "Salva i log su USB", 10: "Avvia Android Auto", 11: "Arresto forzato", 12: "Riavvia",
           13: "Connessione...", 14: "Apri Android Auto", 15: "Avvio automatico", 16: "Informazioni"},
    "pt": {23: "A reproduzir", 22: "Mostrar avisos do iDrive", 21: "Setas de viragem no painel/HUD", 20: "Mostrar mudancas de modo de conducao", 19: "A tecla MENU abre o iDrive", 2: "Android Auto: A ligar...", 18: "Guardar registos em USB", 10: "Iniciar Android Auto", 11: "Forcar paragem", 12: "Reiniciar",
           13: "A ligar...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Sobre"},
}
LANG_TEXTS["es-US"] = LANG_TEXTS["es-ES"]
LANG_TEXTS["fr-CA"] = LANG_TEXTS["fr-FR"]
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Texts.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for lang, langname in LANGS.items():
        texts = dict(TEXTS)
        texts.update(LANG_TEXTS.get(lang, {}))
        body = "100000=%s\n" % langname + "".join("%d=%s\n" % (k, v) for k, v in sorted(texts.items()))
        z.writestr(lang + ".txt", body.encode("utf-8"))
icon = os.path.join(HERE, "..", "plugin", "res", "aa_row_icon_63x54.png")
preview = os.path.join(HERE, "..", "plugin", "res", "aa_content_preview_549x484.png")
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Images.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.write(icon, "55001.png")                      # 63x54 row icon (imageIdModel 4)
    z.write(preview, "55002.png")                   # 549x484 content preview (imageIdModel 12, StatePreview 13)
    for f in range(8):                              # 55010..55017: entry-row "connecting" spinner frames (rhmiapp-6)
        z.write(os.path.join(HERE, "..", "plugin", "res", "spinner", "spin_%d.png" % f), "%d.png" % (55010 + f))

# NbtAad_Media.xml = NbtAad.xml + now playing (upstream #51): a "Now playing" row and screen (audioHmiState), the
# instrument cluster list (instrumentCluster, use case EntICPlaylist) and the iDrive now playing events, with the
# element and attribute names of the Connected Apps' RHMI layout. Ids 200-261, all in the first pluginApp. A separate
# file so a head unit that rejects these elements keeps the plain menu: point DescFile in NbtAad.manifest at it to try.
MEDIA = {
    "</actions>": """      <hmiAction id="200" target="210"/>
      <raAction id="201"/>
      <raAction id="202"/>
      <raAction id="203"/>
      <raAction id="204"/>
      <raAction id="205"/>
      <raAction id="206"/>
      <raAction id="207"/>
""",
    "</models>": """      <textIdModel id="220" textId="23">
        <synonyms/>
      </textIdModel>
      <raListModel id="221" modelType="EntICPlaylist"/>
      <raListModel id="222" modelType="EntICDetails"/>
      <raDataModel id="223"/>
      <raBoolModel id="224"/>
      <textIdModel id="225" textId="23">
        <synonyms/>
      </textIdModel>
      <raDataModel id="226"/>
      <raDataModel id="227"/>
      <raDataModel id="228"/>
      <raDataModel id="229"/>
      <raDataModel id="230"/>
      <raDataModel id="231"/>
      <raDataModel id="232"/>
      <raDataModel id="233"/>
      <raImageModel id="234"/>
      <raDataModel id="235"/>
      <raImageModel id="236"/>
      <raDataModel id="237"/>
      <raImageModel id="238"/>
      <raGaugeModel id="239" modelType="Progress" min="0" max="100" value="0" increment="1"/>
      <raGaugeModel id="240" modelType="Progress" min="0" max="100" value="0" increment="1"/>
      <raDataModel id="241"/>
      <raDataModel id="242"/>
      <raIntModel id="243" value="0"/>
      <raImageModel id="244"/>
      <raImageModel id="245"/>
      <raListModel id="246"/>
      <raDataModel id="247"/>
      <raDataModel id="248"/>
      <raDataModel id="249"/>
""",
    "</events>": """      <multimediaInfoEvent id="260" textModel1="247" textModel2="248"/>
      <statusbarEvent id="261" textModel="249"/>
""",
    "</hmiStates>": """      <audioHmiState id="210" parentId="10" text="Now playing" textModel="220" artistAction="203" coverAction="204" progressAction="205" playListAction="206" albumAction="207" alternativeTextModel="230" trackTextModel="231" playListProgressTextModel="232" playListTextModel="233" artistImageModel="234" artistTextModel="235" albumImageModel="236" albumTextModel="237" coverImageModel="238" playbackProgressModel="239" downloadProgressModel="240" currentTimeModel="241" elapsingTimeModel="242" playListFocusRowModel="243" providerLogoImageModel="244" statusBarImageModel="245" playListModel="246">
        <components/>
      </audioHmiState>
""",
}
xml = open(os.path.join(HERE, "NbtAad.xml"), encoding="utf-8").read()
for tag, block in MEDIA.items():
    i = xml.index("    " + tag)                       # first = the Android Auto pluginApp
    xml = xml[:i] + block + xml[i:]
row = '          <button action="25" id="65" model="36" text="About"/>'
assert xml.count(row) == 1
xml = xml.replace(row, '          <button action="200" id="251" model="220" text="Now playing"/>\n' + row)
eb = "    </entryButton>\n"
i = xml.index(eb) + len(eb)
xml = xml[:i] + ('    <instrumentCluster id="250" playlistModel="221" detailsModel="222" useCaseModel="223" action="201" '
                 'textModel="227" additionalTextModel="226" setTrackAction="202" iSpeechSupport="224" iSpeechText="225" '
                 'skipForward="229" skipBackward="228"/>\n') + xml[i:]
with open(os.path.join(HERE, "NbtAad_Media.xml"), "w", encoding="utf-8") as f:
    f.write(xml)
print("wrote NbtAad_ALL_Texts.zip, NbtAad_ALL_Images.zip, NbtAad_Media.xml")
