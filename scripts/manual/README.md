# Manual generation tools

These tools regenerate and review the editable PDW user manual:

- `build_pdw_manual.py` builds the DOCX from synthetic text and the versioned
  images under `docs/manual/screenshots`.
- `word_to_pdf.ps1` converts a DOCX to PDF through an installed Microsoft Word
  instance.
- `make_contact_sheet.py` creates labelled page-image sheets for visual QA.
- `soffice.cmd` and `soffice_word.cs` are source adapters for render tooling
  that expects a `soffice` command but uses Microsoft Word on Windows.

The Python scripts require `python-docx` and Pillow. The converter requires a
locally installed Microsoft Word. Generated documents, page images, helper
executables, and render directories belong in an ignored output directory and
must not be committed except for the reviewed canonical DOCX and root PDF.
