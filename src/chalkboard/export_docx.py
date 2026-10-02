"""Dependency-free .docx output (opens in Word, Google Docs, LibreOffice, Pages)."""

import re
import zipfile
from datetime import datetime, timezone

from .fontmetrics import WIDTHS
from .markup import links, plain, runs as mark_runs
from .organizers import has_heads, heads_of, shapes, table_cols, table_rows


def escape(s):
    """XML-safe text. (Not xml.sax.saxutils: importing that pulls in Python's networking modules.)"""
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


PAGES = {"Letter": (12240, 15840), "A4": (11906, 16838)}
MARGIN = 1080            # 0.75in in twips
QIND = 400               # question hanging indent
LINE_H = 480             # writing line height (1/3 in)
RED = "B01010"
GRAY = "666666"
_BAD = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f]")

W_NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
        'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
        'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
        'xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" '
        'xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape"')
EMU = 12700  # per point


def x(s):
    return escape(_BAD.sub("", s or ""))


LINKS = []  # web addresses in the document being written; render_docx turns them into relationships


def rich(text, b=False, i=False, size=None, color=None):
    """Runs for text with **bold**, *italic*, __underline__ marks; web addresses become links."""
    out = []
    for piece, bold, italic, under in mark_runs(text or ""):
        for part, url in links(piece):
            if url:
                LINKS.append(url)
                out.append(f'<w:hyperlink r:id="rIdLink{len(LINKS)}" w:history="1">'
                           f'{run(part, b or bold, i or italic, size, "0563C1", True)}</w:hyperlink>')
            else:
                out.append(run(part, b or bold, i or italic, size, color, under))
    return "".join(out)


def run(text, b=False, i=False, size=None, color=None, u=False):
    rpr = ""
    if b:
        rpr += "<w:b/>"
    if i:
        rpr += "<w:i/>"
    if u:
        rpr += '<w:u w:val="single"/>'
    if color:
        rpr += f'<w:color w:val="{color}"/>'
    if size:
        rpr += f'<w:sz w:val="{int(size * 2)}"/><w:szCs w:val="{int(size * 2)}"/>'
    rpr = f"<w:rPr>{rpr}</w:rPr>" if rpr else ""
    parts = "<w:tab/>".join(f'<w:t xml:space="preserve">{x(p)}</w:t>' if p else "" for p in (text or "").split("\t"))
    return f"<w:r>{rpr}{parts}</w:r>"


def para(runs, align=None, left=0, hanging=0, first=0, right=0, before=0, after=100,
         keep=False, border=False, tabs=None, new_page=False):
    ppr = "<w:pageBreakBefore/>" if new_page else ""
    if keep:
        ppr += "<w:keepNext/>"
    if border:
        ppr += '<w:pBdr><w:bottom w:val="single" w:sz="6" w:space="1" w:color="000000"/></w:pBdr>'
    if tabs:
        ppr += "<w:tabs>" + "".join(f'<w:tab w:val="{v}" w:pos="{p}"/>' for v, p in tabs) + "</w:tabs>"
    ppr += f'<w:spacing w:before="{before}" w:after="{after}"/>'
    if left or hanging or first or right:
        ind = f'w:left="{left}" w:right="{right}"'
        ind += f' w:hanging="{hanging}"' if hanging else (f' w:firstLine="{first}"' if first else "")
        ppr += f"<w:ind {ind}/>"
    if align:
        ppr += f'<w:jc w:val="{align}"/>'
    return f"<w:p><w:pPr>{ppr}</w:pPr>{''.join(runs)}</w:p>"


def table(rows_xml, widths, indent=0, borders=False):
    edge = 'w:val="single" w:sz="4" w:color="000000"' if borders else 'w:val="nil"'
    tbl_borders = "".join(f"<w:{e} {edge}/>" for e in ("top", "left", "bottom", "right", "insideH", "insideV"))
    grid = "".join(f'<w:gridCol w:w="{w}"/>' for w in widths)
    return (f'<w:tbl><w:tblPr><w:tblW w:w="{sum(widths)}" w:type="dxa"/>'
            f'<w:tblInd w:w="{indent}" w:type="dxa"/><w:tblBorders>{tbl_borders}</w:tblBorders>'
            f'<w:tblLayout w:type="fixed"/><w:tblCellMar><w:left w:w="60" w:type="dxa"/>'
            f'<w:right w:w="60" w:type="dxa"/></w:tblCellMar></w:tblPr>'
            f"<w:tblGrid>{grid}</w:tblGrid>{''.join(rows_xml)}</w:tbl>")


def cell(content, width, bottom=False):
    b = '<w:tcBorders><w:bottom w:val="single" w:sz="4" w:color="555555"/></w:tcBorders>' if bottom else ""
    return f'<w:tc><w:tcPr><w:tcW w:w="{width}" w:type="dxa"/>{b}</w:tcPr>{content or "<w:p/>"}</w:tc>'


def row(cells, height=None):
    tr = f'<w:trPr><w:cantSplit/><w:trHeight w:val="{height}" w:hRule="exact"/></w:trPr>' if height else \
        "<w:trPr><w:cantSplit/></w:trPr>"
    return f"<w:tr>{tr}{''.join(cells)}</w:tr>"


def shaded_cell(content, width):
    return (f'<w:tc><w:tcPr><w:tcW w:w="{width}" w:type="dxa"/><w:shd w:val="clear" w:color="auto" w:fill="DDDDDD"/>'
            f'<w:vAlign w:val="center"/></w:tcPr>{content}</w:tc>')


def organizer_table(b, width, height):
    """A table-layout organizer as a real Word table, so students can type in it."""
    cols, rows = table_cols(b), table_rows(b)
    side = (b.get("side") or [])[:rows]
    sidecol = b.get("sidecol") or any(s.strip() for s in side)
    sw = width * (24 if cols <= 3 else 18) // 100 if sidecol else 0
    cw = (width - sw) // cols
    widths = ([sw] if sidecol else []) + [cw] * cols
    out = []
    head = 440 if has_heads(b) else 0
    if head:
        cells = [cell(para([], after=0, keep=True), sw)] if sidecol else []
        cells += [shaded_cell(para([run(h, b=True)], align="center", after=0, keep=True), cw)
                  for h in heads_of(b, cols)]
        out.append(row(cells, head))
    rh = max(400, (height - head) // rows)
    for r in range(rows):
        keep = r < rows - 1  # keepNext in every row but the last holds the chart on one page
        label = side[r] if r < len(side) else ""
        blank = para([], after=0, keep=keep)
        cells = [cell(para([run(label, b=True)], after=0, keep=keep) if label.strip() else blank, sw)] if sidecol else []
        cells += [cell(blank, cw) for _ in range(cols)]
        out.append(f'<w:tr><w:trPr><w:cantSplit/><w:trHeight w:val="{rh}" w:hRule="atLeast"/></w:trPr>'
                   + "".join(cells) + "</w:tr>")
    return table(out, widths, indent=QIND, borders=True)


def organizer_drawing(b, width, height, ids):
    """Any other organizer as one inline group of Word shapes (ovals, boxes, lines, text boxes)."""
    wpt, hpt = width / 20, height / 20
    kids = []

    def emu(v):
        return int(round(v * EMU))

    def sp(geom, x, y, w, h, line_w=0.0, fill=None, txt="", flip="", arrow=False):
        ids[0] += 1
        fill_xml = (f'<a:solidFill><a:srgbClr val="{"%02X%02X%02X" % tuple(int(c * 255) for c in fill)}"/></a:solidFill>'
                    if fill else "<a:noFill/>")
        ln = (f'<a:ln w="{emu(line_w)}"><a:solidFill><a:srgbClr val="000000"/></a:solidFill>'
              + ('<a:tailEnd type="triangle" w="med" len="med"/>' if arrow else "") + "</a:ln>"
              if line_w else "<a:ln><a:noFill/></a:ln>")
        body = ('<wps:bodyPr rot="0" vert="horz" wrap="square" lIns="0" tIns="0" rIns="0" bIns="0" anchor="t">'
                '<a:noAutofit/></wps:bodyPr>')
        box = f"<wps:txbx><w:txbxContent>{txt}</w:txbxContent></wps:txbx>" if txt else ""
        kids.append(f'<wps:wsp><wps:cNvPr id="{ids[0]}" name="Shape {ids[0]}"/><wps:cNvSpPr/>'
                    f'<wps:spPr><a:xfrm{flip}><a:off x="{emu(x)}" y="{emu(y)}"/><a:ext cx="{max(emu(w), 1)}" '
                    f'cy="{max(emu(h), 1)}"/></a:xfrm><a:prstGeom prst="{geom}"><a:avLst/></a:prstGeom>'
                    f"{fill_xml}{ln}</wps:spPr>{box}{body}</wps:wsp>")

    sp("rect", 0, 0, wpt, hpt)  # an invisible frame, so apps that size a group by its contents keep the shape
    for s in shapes(b, wpt, hpt):
        k = s["k"]
        if k == "rect":
            sp("rect", s["x"], s["y"], s["w"], s["h"], s["lw"], s["fill"])
        elif k == "oval":
            sp("ellipse", s["cx"] - s["rx"], s["cy"] - s["ry"], 2 * s["rx"], 2 * s["ry"], s["lw"], s["fill"])
        elif k == "line":
            pts = s["pts"]
            for i, ((x1, y1), (x2, y2)) in enumerate(zip(pts, pts[1:])):
                flip = (' flipH="1"' if x2 < x1 else "") + (' flipV="1"' if y2 < y1 else "")
                sp("line", min(x1, x2), min(y1, y2), abs(x2 - x1), abs(y2 - y1), s["lw"], flip=flip,
                   arrow=s["arrow"] and i == len(pts) - 2)
        elif k == "text":
            align = "center" if s["align"] == "center" else None
            txt = para([run(plain(s["text"]), b=s["bold"], size=s["size"])], align=align, after=0)
            sp("rect", s["x"], s["y"], s["w"], s["size"] * 3.6, txt=txt)
    ids[0] += 1
    cx, cy = emu(wpt), emu(hpt)
    group = (f'<wpg:wgp><wpg:cNvGrpSpPr/><wpg:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/>'
             f'<a:chOff x="0" y="0"/><a:chExt cx="{cx}" cy="{cy}"/></a:xfrm></wpg:grpSpPr>{"".join(kids)}</wpg:wgp>')
    drawing = (f'<w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx="{cx}" cy="{cy}"/>'
               f'<wp:docPr id="{ids[0]}" name="Organizer {ids[0]}"/><wp:cNvGraphicFramePr/>'
               f'<a:graphic><a:graphicData uri="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup">'
               f"{group}</a:graphicData></a:graphic></wp:inline></w:drawing>")
    return f"<w:p><w:pPr><w:spacing w:before=\"60\" w:after=\"60\"/><w:ind w:left=\"{QIND}\"/></w:pPr><w:r>{drawing}</w:r></w:p>"


def fill_height(used, body_h):
    """Twips left on the page for a fill block, leaving room for Word's closing paragraph."""
    return max(1440, body_h - used - 700)


def body_xml(blocks, width, body_h):
    out = []
    used = 0  # rough twips used on the current page, for the blocks fixed-layout sheets put above a fill
    new_page = False
    ids = [0]  # drawing object ids, unique in the document
    for i, b in enumerate(blocks):
        t = b["t"]
        used += {"title": 520, "subtitle": 460, "fields": 800, "h1": 600}.get(t, 0)
        if t == "p":
            used += 360 * (len(b["text"]) // 95 + 1)
        if t == "title":
            out.append(para([run(plain(b["text"]), b=True, size=17)], align="center", after=40, keep=True,
                            new_page=new_page))
            new_page = False
        elif t == "subtitle":
            out.append(para([run(plain(b["text"]), i=True, size=10.5, color=GRAY)], align="center", after=200))
        elif t == "fields" and b.get("shares"):
            # label + underlined blank per item, sized like the PDF's shares
            cells, widths = [], []
            for k, share in zip(b["items"], b["shares"]):
                seg = int(width * share)
                text_w = sum(WIDTHS["Helvetica-Bold"][ord(c) - 32] for c in k + ":" if 32 <= ord(c) < 127) * 11 / 1000
                lw = min(seg - 600, int(text_w * 20) + 260)  # widest common font + cell margins
                cells += [cell(para([run(k + ":", b=True)], after=0), lw),
                          cell(None, seg - lw - 240, bottom=True), cell(None, 240)]
                widths += [lw, seg - lw - 240, 240]
            out.append(table([row(cells, 360)], widths, indent=(width - sum(widths)) // 2 if b.get("center") else 0))
            out.append(para([], after=120))
        elif t == "fields":
            sizes = {"Name": 34, "Date": 14, "Period": 7}
            runs = []
            for k in b["items"]:
                runs.append(run(k + ": ", b=True))
                runs.append(run("_" * sizes.get(k, 12) + "     "))
            out.append(para(runs, after=200))
        elif t == "h1":
            out.append(para([run(plain(b["text"]), b=True, size=12.5)], before=240, after=100, keep=True, border=True))
        elif t == "p":
            st = b.get("style", "normal")
            out.append(para([rich(b["text"], b=st == "bold", i=st in ("italic", "small"),
                                 size=9.5 if st == "small" else None, color=GRAY if st == "small" else None)],
                            left=b.get("pad", 0) * 20))
        elif t == "check":
            out.append(para([run("\u2610", size=15), run("\t" + plain(b["text"]), b=True, size=11.5)],
                            left=360, hanging=360, before=160, after=60, keep=True, tabs=[("left", 360)]))
        elif t == "bullet":
            left = 360 + 360 * b.get("indent", 0) + b.get("pad", 0) * 20
            out.append(para([run("•\t"), rich(b["text"])], left=left, hanging=240, after=60,
                            tabs=[("left", left)]))
        elif t == "kv":
            out.append(para([run(plain(b["label"]) + " ", b=True), rich(b["text"])]))
        elif t == "q":
            runs = [run(b["num"], b=True), run("\t"), rich(b["text"])]
            if b.get("points"):
                runs.append(run("  " + b["points"], i=True, color=GRAY))
            out.append(para(runs, left=QIND, hanging=QIND, before=160, after=60, keep=True,
                            tabs=[("left", QIND)]))
        elif t == "choice":
            c = RED if b["correct"] else None
            label = ("✔ " if b["correct"] else "") + b["label"]
            out.append(para([run(label, b=b["correct"], color=c), run("  "), rich(b["text"], b=b["correct"], color=c)],
                            left=QIND + 640, hanging=400, after=40, keep=not b.get("last")))
        elif t == "choice_inline":
            runs = []
            for i, item in enumerate(b["items"]):
                hit = b["correct"] == i
                runs.append(run(("✔ " if hit else "") + item, b=hit, color=RED if hit else None))
                runs.append(run("          "))
            out.append(para(runs, left=QIND + 240, after=80))
        elif t in ("lines", "blank", "box"):
            w = width - QIND
            if t == "lines":
                rows = [row([cell(None, w, bottom=True)], LINE_H) for _ in range(b["n"])]
                out.append(table(rows, [w], indent=QIND))
            else:
                out.append(table([row([cell(None, w)], LINE_H * b["n"])], [w], indent=QIND, borders=(t == "box")))
            out.append(para([], after=0))
        elif t == "answer":
            out.append(para([run("Answer: ", b=True, i=True, color=RED), rich(b["text"], i=True, color=RED)],
                            left=QIND))
        elif t == "match":
            lw = (width - QIND) * 42 // 100
            rw = width - QIND - lw
            rows = []
            n = max(len(b["left"]), len(b["right"]))
            for i in range(n):
                left = b["left"][i] if i < len(b["left"]) else ""
                right = b["right"][i] if i < len(b["right"]) else ""
                if left:
                    blank = [run(f"  {b['key'][i]}  ", b=True, color=RED)] if b["key"] else [run("______")]
                    lp = para(blank + [run("   "), rich(left)], after=60)
                else:
                    lp = None
                rp = para([run(f"{'ABCDEFGHIJ'[i] if i < 10 else '?'}. ", b=True), rich(right)], after=60) if right else None
                rows.append(row([cell(lp, lw), cell(rp, rw)]))
            out.append(table(rows, [lw, rw], indent=QIND))
            out.append(para([], after=0))
        elif t == "organizer":
            # "fill the page" can't be measured in Word; use a big chart that still fits under a heading
            h = min(LINE_H * (max(b["n"], 22) if b.get("fill") else b["n"]), body_h - 1600)
            if b.get("layout") == "table":
                out.append(organizer_table(b, width - QIND, h))
                out.append(para([], after=0))
            else:
                out.append(organizer_drawing(b, width - QIND, h, ids))
        elif t == "passage":
            if b.get("title"):
                out.append(para([run(plain(b["title"]), b=True, size=12)], align="center", before=200, after=100,
                                keep=True))
            for p in (b.get("text") or "").split("\n"):
                if p.strip():
                    out.append(para([rich(p.strip())], left=360, right=360, first=360, after=80))
        elif t == "grid":
            widths = [int(width * share) for _, share in b["cols"]]
            head = row([shaded_cell(para([run(label, b=True)], align="center", after=0), w)
                        for (label, _), w in zip(b["cols"], widths)], 440)
            rh = (fill_height(used, body_h) - 440) // b["rows"]
            # "atLeast" so a row grows as a student types into it
            body = [f'<w:tr><w:trPr><w:trHeight w:val="{rh}" w:hRule="atLeast"/></w:trPr>'
                    + "".join(cell(None, w) for w in widths) + "</w:tr>" for _ in range(b["rows"])]
            out.append(table([head] + body, widths, borders=True))
        elif t == "days":
            bh = fill_height(used, body_h) // len(b["days"])
            rows = []
            for label, prompt in b["days"]:
                content = para([run(label, b=True, size=12), run("\t"), run("Date: ", b=True, size=10),
                                run("____________", size=10)], after=60, tabs=[("right", width - 160)])
                if prompt.strip():
                    content += para([rich(prompt.strip(), i=True)], after=60)
                rows.append(row([cell(content, width)], bh))
            out.append(table(rows, [width], borders=True))
        elif t == "space":
            pass
        elif t == "rule":
            out.append(para([], border=True))
        elif t == "pagebreak":
            # a following title starts the page itself, so no extra paragraph spills onto the page before
            if i + 1 < len(blocks) and blocks[i + 1]["t"] == "title":
                new_page = True
            else:
                out.append('<w:p><w:r><w:br w:type="page"/></w:r></w:p>')
            used = 0
    out.append(para([]))
    return "".join(out)


def render_docx(doc, path, family="Times", page="Letter"):
    font = "Times New Roman" if family == "Times" else "Arial"
    pw, ph = PAGES.get(page, PAGES["Letter"])
    width = pw - 2 * MARGIN
    LINKS.clear()
    body = body_xml(doc["blocks"], width, ph - 2 * MARGIN)
    sect = (f'<w:sectPr><w:footerReference w:type="default" r:id="rId2"/>'
            f'<w:pgSz w:w="{pw}" w:h="{ph}"/>'
            f'<w:pgMar w:top="{MARGIN}" w:right="{MARGIN}" w:bottom="{MARGIN}" w:left="{MARGIN}" '
            f'w:header="720" w:footer="500" w:gutter="0"/></w:sectPr>')
    document = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
                f'<w:document {W_NS}><w:body>{body}{sect}</w:body></w:document>')
    small = f'<w:rPr><w:color w:val="{GRAY}"/><w:sz w:val="17"/><w:szCs w:val="17"/></w:rPr>'
    fld = lambda instr: (f'<w:r>{small}<w:fldChar w:fldCharType="begin"/></w:r>'
                         f'<w:r>{small}<w:instrText xml:space="preserve"> {instr} </w:instrText></w:r>'
                         f'<w:r>{small}<w:fldChar w:fldCharType="separate"/></w:r>{run("1", size=8.5, color=GRAY)}'
                         f'<w:r>{small}<w:fldChar w:fldCharType="end"/></w:r>')
    footer = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:ftr {W_NS}>'
              f'<w:p><w:pPr><w:tabs><w:tab w:val="right" w:pos="{width}"/></w:tabs></w:pPr>'
              f'{run(doc.get("footer", ""), i=True, size=8.5, color=GRAY)}<w:r><w:tab/></w:r>'
              f'{run("Page ", size=8.5, color=GRAY)}{fld("PAGE")}{run(" of ", size=8.5, color=GRAY)}{fld("NUMPAGES")}'
              f'</w:p></w:ftr>')
    styles = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:styles {W_NS}>'
              f'<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="{font}" w:hAnsi="{font}" w:cs="{font}" w:eastAsia="{font}"/>'
              f'<w:sz w:val="22"/><w:szCs w:val="22"/><w:lang w:val="en-US"/></w:rPr></w:rPrDefault>'
              f'<w:pPrDefault><w:pPr><w:spacing w:after="100" w:line="264" w:lineRule="auto"/></w:pPr></w:pPrDefault>'
              f'</w:docDefaults><w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>'
              f'<w:style w:type="table" w:default="1" w:styleId="TableNormal"><w:name w:val="Normal Table"/>'
              f'<w:tblPr><w:tblInd w:w="0" w:type="dxa"/><w:tblCellMar><w:top w:w="0" w:type="dxa"/>'
              f'<w:left w:w="108" w:type="dxa"/><w:bottom w:w="0" w:type="dxa"/><w:right w:w="108" w:type="dxa"/>'
              f'</w:tblCellMar></w:tblPr></w:style></w:styles>')
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    core = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" '
            'xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" '
            'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">'
            f'<dc:title>{x(doc.get("title", ""))}</dc:title><dc:creator>Chalkboard</dc:creator>'
            f'<dcterms:created xsi:type="dcterms:W3CDTF">{stamp}</dcterms:created>'
            f'<dcterms:modified xsi:type="dcterms:W3CDTF">{stamp}</dcterms:modified></cp:coreProperties>')
    app = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
           '<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties">'
           '<Application>Chalkboard</Application></Properties>')
    ctypes = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
              '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
              '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
              '<Default Extension="xml" ContentType="application/xml"/>'
              '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
              '<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>'
              '<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>'
              '<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>'
              '<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>'
              '</Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
            '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>'
            '<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>'
            '</Relationships>')
    doc_rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
                '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
                '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/>'
                + "".join(f'<Relationship Id="rIdLink{n}" Type="http://schemas.openxmlformats.org/officeDocument/2006/'
                          f'relationships/hyperlink" Target="{escape(u)}" TargetMode="External"/>'
                          for n, u in enumerate(LINKS, 1)) +
                '</Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ctypes)
        z.writestr("_rels/.rels", rels)
        z.writestr("word/document.xml", document)
        z.writestr("word/_rels/document.xml.rels", doc_rels)
        z.writestr("word/styles.xml", styles)
        z.writestr("word/footer1.xml", footer)
        z.writestr("docProps/core.xml", core)
        z.writestr("docProps/app.xml", app)
