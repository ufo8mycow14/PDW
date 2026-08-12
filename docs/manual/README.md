# Editable user manual source

`PDW-v5.5.2-2026-Release-User-Manual.docx` is the editable source for the
repository-root `pdw-manual.pdf`.

The deterministic document builder is
`scripts/manual/build_pdw_manual.py`. Its synthetic source screenshots are in
`docs/manual/screenshots` so the manual does not depend on an ignored build or
temporary directory.

Before replacing the published PDF:

1. render the DOCX to PDF and page images;
2. visually inspect every rendered page;
3. run the accessibility audit;
4. confirm the rendered PDF is the intended replacement; and
5. commit the DOCX, accessibility report, and root PDF together.

The generated PDF is not duplicated in this directory. The root
`pdw-manual.pdf` is the canonical rendered copy distributed from the
repository.

From the repository root, regenerate the DOCX with:

```powershell
python scripts/manual/build_pdw_manual.py `
  --output docs/manual/PDW-v5.5.2-2026-Release-User-Manual.docx
```

`scripts/manual/word_to_pdf.ps1` provides the Microsoft Word PDF conversion
used on Windows. `make_contact_sheet.py` creates labelled page sheets for
visual review. The optional `soffice` adapter source is retained for renderers
that expect that command name; its generated executable must not be committed.
