#!/usr/bin/env python3
"""Kleiner, auf die docs/*.md dieses Projekts zugeschnittener Markdown-zu-
HTML-Konverter fuer die In-App-Hilfe (web/help/*.html). Bewusst ohne externe
Abhaengigkeit (kein pip/npm-Paket), da das Projekt einen "kein Build-Schritt
fuers Web-UI"-Ansatz verfolgt - dieses Skript ist reines Entwickler-Tooling,
laeuft nicht auf dem Geraet.

Unterstuetzt genau das Markdown-Subset, das in docs/*.md verwendet wird:
ATX-Ueberschriften (#..###) mit GitHub-kompatiblen Anchor-IDs, GFM-Tabellen,
Fenced Code-Bloecke, Blockquotes, ungeordnete/geordnete Listen, horizontale
Linien, sowie Inline: **fett**, `code`, [text](url).

Usage: python md_to_help_html.py <input.md> <output.html>
"""
import html
import re
import sys


def slugify(text):
    # GitHub-Anchor-Algorithmus: Markdown-Inline-Syntax entfernen, lowercase,
    # alles ausser Buchstaben/Ziffern/Leerzeichen/Bindestrich entfernen,
    # Leerzeichen -> Bindestrich.
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\*\*([^*]*)\*\*", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = text.strip().lower()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    text = re.sub(r"\s+", "-", text)
    return text


def inline(text):
    text = html.escape(text, quote=False)
    # Links zuerst (koennen **/`` enthalten)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: (
        f'<a href="{m.group(2)}"'
        + (' target="_blank" rel="noopener"' if m.group(2).startswith("http") else "")
        + f'>{m.group(1)}</a>'
    ), text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    # Einfache Italic-Syntax (*text*) NACH Bold behandeln, damit uebrig
    # gebliebene einzelne "*" von **-Paaren nicht faelschlich matchen.
    text = re.sub(r"(?<!\*)\*([^*\n]+)\*(?!\*)", r"<em>\1</em>", text)
    return text


BLOCK_STARTER_RE = re.compile(
    r"^(#{1,4}\s|```|\||>\s|>$|-{3,}$|\*{3,}$|[-*]\s+|\d+\.\s+)"
)


def split_row(line):
    # Wie GFM: "|" trennt Zellen, ausser wenn es mit "\" escaped ist (z.B.
    # "`dallas` \| `dht11`" in einer einzelnen Zelle). Nach dem Splitten wird
    # "\|" wieder zu einem literalen "|" entschaerft.
    cells = re.split(r"(?<!\\)\|", line.strip())
    cells = [c.strip() for c in cells]
    if cells and cells[0] == "":
        cells = cells[1:]
    if cells and cells[-1] == "":
        cells = cells[:-1]
    return [c.replace("\\|", "|") for c in cells]


def render_table(lines):
    header = split_row(lines[0])
    rows = [split_row(ln) for ln in lines[2:]]
    out = ['<div class="help-table-wrap"><table>', "<thead><tr>"]
    for c in header:
        out.append(f"<th>{inline(c)}</th>")
    out.append("</tr></thead><tbody>")
    for row in rows:
        out.append("<tr>")
        for c in row:
            out.append(f"<td>{inline(c)}</td>")
        out.append("</tr>")
    out.append("</tbody></table></div>")
    return "\n".join(out)


def convert(md_text):
    lines = md_text.split("\n")
    out = []
    i = 0
    n = len(lines)
    used_ids = set()

    def uniq_id(base):
        if base not in used_ids:
            used_ids.add(base)
            return base
        k = 2
        while f"{base}-{k}" in used_ids:
            k += 1
        used_ids.add(f"{base}-{k}")
        return f"{base}-{k}"

    paragraph_buf = []

    def flush_paragraph():
        if paragraph_buf:
            text = " ".join(paragraph_buf).strip()
            if text:
                out.append(f"<p>{inline(text)}</p>")
            paragraph_buf.clear()

    while i < n:
        line = lines[i]
        stripped = line.strip()

        # Fenced code block
        if stripped.startswith("```"):
            flush_paragraph()
            lang = stripped[3:].strip()
            code_lines = []
            i += 1
            while i < n and not lines[i].strip().startswith("```"):
                code_lines.append(lines[i])
                i += 1
            i += 1  # schliessendes ```
            code = html.escape("\n".join(code_lines))
            cls = f' class="language-{lang}"' if lang else ""
            out.append(f'<pre><code{cls}>{code}</code></pre>')
            continue

        # ATX-Ueberschrift
        m = re.match(r"^(#{1,4})\s+(.*)$", line)
        if m:
            flush_paragraph()
            level = len(m.group(1))
            text = m.group(2).strip()
            anchor = uniq_id(slugify(text))
            out.append(f'<h{level} id="{anchor}">{inline(text)}</h{level}>')
            i += 1
            continue

        # Horizontale Linie (nicht Tabellen-Trennzeile - die hat "|")
        if re.match(r"^-{3,}$", stripped) or re.match(r"^\*{3,}$", stripped):
            flush_paragraph()
            out.append("<hr>")
            i += 1
            continue

        # GFM-Tabelle: Kopfzeile + Trennzeile "|---|---|"
        if stripped.startswith("|") and i + 1 < n and re.match(r"^\|?[\s:|-]+\|?$", lines[i + 1].strip()) and "-" in lines[i + 1]:
            flush_paragraph()
            table_lines = [lines[i], lines[i + 1]]
            j = i + 2
            while j < n and lines[j].strip().startswith("|"):
                table_lines.append(lines[j])
                j += 1
            out.append(render_table(table_lines))
            i = j
            continue

        # Blockquote (fortlaufende ">"-Zeilen zusammenfassen)
        if stripped.startswith(">"):
            flush_paragraph()
            quote_lines = []
            while i < n and lines[i].strip().startswith(">"):
                quote_lines.append(re.sub(r"^\s*>\s?", "", lines[i]))
                i += 1
            out.append(f"<blockquote><p>{inline(' '.join(quote_lines).strip())}</p></blockquote>")
            continue

        # Ungeordnete/geordnete Liste. Folgezeilen, die selbst keinen neuen
        # Block eroeffnen (kein neues "-"/"1."/Ueberschrift/Codeblock/...),
        # gehoeren per "lazy continuation" (wie in CommonMark) noch zum
        # letzten Listenpunkt und werden angehaengt, statt einen eigenen
        # Absatz zu bilden - sonst reissen mehrzeilige Stichpunkte aus docs/
        # (z.B. "- **Foo:** ...\n  Fortsetzungstext ...") mitten durch.
        is_ul = re.match(r"^[-*]\s+", stripped)
        is_ol = re.match(r"^\d+\.\s+", stripped)
        if is_ul or is_ol:
            flush_paragraph()
            bullet_re = r"^[-*]\s+" if is_ul else r"^\d+\.\s+"
            items = []
            while i < n:
                cur = lines[i].strip()
                if re.match(bullet_re, cur):
                    items.append(re.sub(bullet_re, "", cur))
                    i += 1
                elif cur and items and not BLOCK_STARTER_RE.match(cur):
                    items[-1] += " " + cur
                    i += 1
                else:
                    break
            tag = "ul" if is_ul else "ol"
            out.append(f"<{tag}>" + "".join(f"<li>{inline(it)}</li>" for it in items) + f"</{tag}>")
            continue

        # Leerzeile -> Absatz abschliessen
        if stripped == "":
            flush_paragraph()
            i += 1
            continue

        # Fliesstext
        paragraph_buf.append(stripped)
        i += 1

    flush_paragraph()
    return "\n".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python md_to_help_html.py <input.md> <output.html>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        md = f.read()
    html_out = convert(md)
    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as f:
        f.write(html_out)
    print(f"Written: {sys.argv[2]}")
