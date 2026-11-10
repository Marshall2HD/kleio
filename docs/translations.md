# Translations

Locket speaks the 43 languages Maccy is translated into: Arabic, Belarusian, Bengali, Bosnian,
Catalan, Central Kurdish, Chinese (Simplified and Traditional), Croatian, Czech, Dutch, English,
Esperanto, Finnish, French, German, Greek, Hebrew, Hindi, Hungarian, Indonesian, Italian, Japanese,
Khmer, Korean, Latvian, Lithuanian, Norwegian Bokmål, Persian, Polish, Portuguese (Brazil and
Portugal), Romanian, Russian, Slovenian, Spanish, Swedish, Tamil, Thai, Turkish, Ukrainian, Uzbek
and Vietnamese. Right-to-left languages are laid out right to left.

By default Locket follows the Windows display language. *Settings → General → Language* picks
another one for Locket only (restart Locket to apply it), like a per-app language on macOS.
Shortcuts are shown with the key names printed on keyboards in that language (*Strg+Umschalt+C*
in German); settings and the command line always use the English names.

The strings live in `src/Kleio/Strings/<language>/Resources.resw`, one file per language; `en-US`
is the source. Wherever Maccy has the same text, the translation comes from
[Maccy's translations on Weblate](https://hosted.weblate.org/projects/maccy/), adapted to Windows
(*Ctrl* instead of ⌘, *notification area* instead of menu bar, *Settings* instead of Preferences).
The Windows-specific texts and the gaps in Maccy's translations were translated for Locket and
deserve a review by native speakers.

## Contributing a translation

To improve a translation, edit the `<value>` of a key in the language's `Resources.resw` (any text
editor, or the resource editor of Visual Studio) and open a pull request. Keep `{0}`-style
placeholders and terms such as `--get` or `Kleio.Transient` as they are, and use the key names of
your keyboard in shortcuts. To add a language, copy `en-US` to a new folder named with the
BCP-47 tag Windows uses (for example `sk` or `sr-Latn`), translate it and add the language to the
list in `src/Kleio/Strings.cpp`. Then run:

```powershell
python tools/check_translations.py
```

It checks that every file parses, has exactly the keys of `en-US` with the same placeholders, and
that the code and XAML only use keys that exist. CI runs it too.

The command-line help (`locket --help`) stays in English; its error messages are translated.
