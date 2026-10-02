"""Dependency-free .pptx slideshow of the board slides, fully editable.

The board layout (export_png) draws onto a SlideCanvas instead of a PDF page: every panel,
the title, the date, and the class codes become real text boxes a teacher can click into
and change (next year's dates, a new class code, another standard), and web addresses
become links. Opens in PowerPoint, Keynote, LibreOffice Impress, and Google Slides (upload
to Drive, then Open with Google Slides).
"""

import zlib
import struct
from datetime import datetime, timezone

from .export_pdf import Fonts
from .markup import links, plain, runs


def escape(s):
    """XML-safe text. (Not xml.sax.saxutils: importing that pulls in Python's networking modules.)"""
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


CX, CY = 12192000, 6858000  # 13.333 x 7.5 in, PowerPoint's widescreen size

P_NS = ('xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
        'xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"')
REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
CORE_REL = "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"
HEAD = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
EMPTY_TREE = ('<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>'
              '<p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/>'
              '<a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld>')


def rels(items):
    """items: [(id, type, target)]; a type is short for REL/type unless it is a full URL."""
    body = "".join(f'<Relationship Id="{i}" Type="{t if t.startswith("http") else REL + "/" + t}" '
                   f'Target="{target}"/>' for i, t, target in items)
    return HEAD + f'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">{body}</Relationships>'


PX = CX // 1920        # EMU per board pixel (the layout is 1920 x 1080, y up from the bottom)
ASCENT = 0.905         # Arial's ascent: from the top of a text box to the first baseline, per font size
R, B, I, BI = 0, 1, 2, 3  # export_pdf's styles


def _hex(color):
    return "%02X%02X%02X" % tuple(round(max(0.0, min(1.0, v)) * 255) for v in color)


_FONTS = Fonts("Helvetica")


def _width(text, style, size):
    """Width of one line in board pixels (Arial has Helvetica's widths)."""
    return _FONTS.width(plain(text), style, size)


def _sz(px):
    return max(100, round(px * 50))  # board pixels -> hundredths of a point (1920 px = 960 pt)


class SlideCanvas:
    """Takes the board's drawing calls (like export_pdf.Canvas) and makes editable slide shapes."""
    editable = True

    def __init__(self):
        self.shapes, self.media, self.links = [], [], []
        self.bg = None

    def _id(self):
        return len(self.shapes) + 2

    def _xfrm(self, x, top, w, h):
        return (f'<a:xfrm><a:off x="{round(x * PX)}" y="{round((1080 - top) * PX)}"/>'
                f'<a:ext cx="{max(1, round(w * PX))}" cy="{max(1, round(h * PX))}"/></a:xfrm>')

    # -- the PDF canvas calls
    def fill(self, x, y, w, h, color):
        if x <= 0 and y <= 0 and w >= 1920 and h >= 1080:
            self.bg = color
            return
        n = self._id()
        self.shapes.append(
            f'<p:sp><p:nvSpPr><p:cNvPr id="{n}" name="Panel {n}"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr>'
            f'<p:spPr>{self._xfrm(x, y + h, w, h)}<a:prstGeom prst="rect"><a:avLst/></a:prstGeom>'
            f'<a:solidFill><a:srgbClr val="{_hex(color)}"/></a:solidFill><a:ln><a:noFill/></a:ln></p:spPr></p:sp>')

    def line(self, x1, y1, x2, y2, w=0.5, color=(0, 0, 0)):
        n = self._id()
        x, top = min(x1, x2), max(y1, y2)
        self.shapes.append(
            f'<p:cxnSp><p:nvCxnSpPr><p:cNvPr id="{n}" name="Line {n}"/><p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>'
            f'<p:spPr>{self._xfrm(x, top, abs(x2 - x1), abs(y2 - y1))}<a:prstGeom prst="line"><a:avLst/></a:prstGeom>'
            f'<a:ln w="{round(w * PX)}"><a:solidFill><a:srgbClr val="{_hex(color)}"/></a:solidFill></a:ln>'
            '</p:spPr></p:cxnSp>')

    def image(self, img, x, y, w, h):
        data, ext = image_file(img)
        self.media.append((data, ext))
        rid = f"rIdImg{len(self.media)}"
        n = self._id()
        self.shapes.append(
            f'<p:pic><p:nvPicPr><p:cNvPr id="{n}" name="School logo" descr="School logo"/>'
            '<p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>'
            f'<p:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>'
            f'<p:spPr>{self._xfrm(x, y + h, w, h)}<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic>')

    def text(self, x, y, s, style=R, size=11, color=None):
        """One line at baseline y: a text box that doesn't wrap."""
        if s:
            self.textbox(x, y + size * ASCENT, None, size * 1.2, [[(s, style, size, color)]])

    def clip(self, *a):
        pass

    def unclip(self):
        pass

    # -- editable text
    def run(self, text, style, size, color, under=False):
        out = []
        for piece, url in links(text):
            link = ""
            if url:
                self.links.append(url)
                link = f'<a:hlinkClick r:id="rIdLink{len(self.links)}"/>'
            u = ' u="sng"' if under or url else ""
            out.append(
                f'<a:r><a:rPr lang="en-US" sz="{_sz(size)}" b="{int(style in (B, BI))}" i="{int(style in (I, BI))}"'
                f'{u} dirty="0"><a:solidFill><a:srgbClr val="{_hex(color or (0, 0, 0))}"/>'
                f'</a:solidFill><a:latin typeface="Arial"/>{link}</a:rPr><a:t>{escape(piece)}</a:t></a:r>')
        return "".join(out)

    def rich(self, text, base, size, color):
        """Text with **bold** / *italic* / __underline__ marks as runs."""
        out = []
        for piece, bold, italic, under in runs(text):
            style = (B if bold or base in (B, BI) else R) + (I if italic or base in (I, BI) else 0)
            out.append(self.run(piece, style, size, color, under))
        return "".join(out)

    def textbox(self, x, top, w, h, paras, align="l", name="Text"):
        """paras: [[(text, style, size, color)]] (one list of pieces per paragraph). w=None: one line, as wide
        as its text (with room to grow a little when edited)."""
        body = "".join(f'<a:p><a:pPr algn="{align}"/>'
                       + "".join(self.rich(t, st, sz, col) for t, st, sz, col in para) + '</a:p>' for para in paras)
        wrap = w is not None
        if not wrap:
            w = max(sum(_width(t, st, sz) for t, st, sz, _ in para) for para in paras) * 1.04 + 4
        self._box(x, top, w, h, body, wrap=wrap, name=name)

    def blocks(self, x, top, w, h, blocks, size, color, name="Text"):
        """A panel's board blocks (bullets, paragraphs, label: text) as wrapping paragraphs."""
        lead = f'<a:lnSpc><a:spcPts val="{_sz(size * 1.24)}"/></a:lnSpc>'
        out, prev = [], None
        for b in blocks:
            gap = 0 if prev is None else size * (0.22 if b["t"] == prev == "bullet" else 0.55)
            prev = b["t"]
            before = f'<a:spcBef><a:spcPts val="{_sz(gap)}"/></a:spcBef>'
            if b["t"] == "bullet":
                ind = size * (1.05 + 1.05 * b.get("indent", 0))
                ppr = (f'<a:pPr marL="{round(ind * PX)}" indent="{-round(size * 0.8 * PX)}">{lead}{before}'
                       '<a:buFont typeface="Arial"/><a:buChar char="&#8226;"/></a:pPr>')
                body = self.rich(b["text"], R, size, color)
            elif b["t"] == "kv":
                ppr = f'<a:pPr>{lead}{before}<a:buNone/></a:pPr>'
                body = self.run(b["label"] + "  ", B, size, color) + self.rich(b["text"], R, size, color)
            else:
                ppr = f'<a:pPr>{lead}{before}<a:buNone/></a:pPr>'
                body = self.rich(b["text"], B if b.get("style") == "bold" else R, size, color)
            out.append(f"<a:p>{ppr}{body}</a:p>")
        self._box(x, top, w, h, "".join(out), wrap=True, name=name, fit=True)

    def _box(self, x, top, w, h, paras, wrap, name, fit=False):
        n = self._id()
        body = (f'<a:bodyPr wrap="{"square" if wrap else "none"}" lIns="0" tIns="0" rIns="0" bIns="0" anchor="t">'
                + ('<a:normAutofit/>' if fit else '<a:spAutoFit/>' if not wrap else '') + '</a:bodyPr>')
        self.shapes.append(
            f'<p:sp><p:nvSpPr><p:cNvPr id="{n}" name="{escape(name)}"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr>'
            f'<p:spPr>{self._xfrm(x, top, w, h)}<a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr>'
            f'<p:txBody>{body}<a:lstStyle/>{paras}</p:txBody></p:sp>')

    def xml(self):
        bg = (f'<p:bg><p:bgPr><a:solidFill><a:srgbClr val="{_hex(self.bg)}"/></a:solidFill><a:effectLst/></p:bgPr></p:bg>'
              if self.bg else "")
        return (HEAD + f'<p:sld {P_NS}><p:cSld>{bg}<p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/>'
                '<p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/>'
                '<a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>'
                + "".join(self.shapes) +
                '</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>')


def image_file(img):
    """An images.read_image() picture as file bytes for the deck: JPEG as is, anything else as PNG."""
    if img["filter"] == "DCTDecode":
        return img["data"], "jpeg"
    w, h, c = img["w"], img["h"], img["colors"]
    pix = zlib.decompress(img["data"])
    alpha = zlib.decompress(img["alpha"]) if img.get("alpha") else None
    kind = {1: 0, 3: 2}[c] + (4 if alpha else 0)  # gray / RGB, with or without alpha
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        line = pix[y * w * c:(y + 1) * w * c]
        if alpha:
            a = alpha[y * w:(y + 1) * w]
            for x in range(w):
                rows += line[x * c:(x + 1) * c] + a[x:x + 1]
        else:
            rows += line

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, kind, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")), "png"


THEME = (HEAD + '<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Chalkboard">'
         '<a:themeElements><a:clrScheme name="Chalkboard">'
         '<a:dk1><a:srgbClr val="000000"/></a:dk1><a:lt1><a:srgbClr val="FFFFFF"/></a:lt1>'
         '<a:dk2><a:srgbClr val="1F3B30"/></a:dk2><a:lt2><a:srgbClr val="F6F3E9"/></a:lt2>'
         '<a:accent1><a:srgbClr val="136B4C"/></a:accent1><a:accent2><a:srgbClr val="F9D26B"/></a:accent2>'
         '<a:accent3><a:srgbClr val="4472C4"/></a:accent3><a:accent4><a:srgbClr val="ED7D31"/></a:accent4>'
         '<a:accent5><a:srgbClr val="A5A5A5"/></a:accent5><a:accent6><a:srgbClr val="70AD47"/></a:accent6>'
         '<a:hlink><a:srgbClr val="LINK"/></a:hlink><a:folHlink><a:srgbClr val="LINK"/></a:folHlink>'
         '</a:clrScheme><a:fontScheme name="Chalkboard">'
         '<a:majorFont><a:latin typeface="Arial"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont>'
         '<a:minorFont><a:latin typeface="Arial"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont>'
         '</a:fontScheme><a:fmtScheme name="Chalkboard"><a:fillStyleLst>'
         + '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>' * 3 +
         '</a:fillStyleLst><a:lnStyleLst>'
         + '<a:ln w="9525"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln>' * 3 +
         '</a:lnStyleLst><a:effectStyleLst>'
         + '<a:effectStyle><a:effectLst/></a:effectStyle>' * 3 +
         '</a:effectStyleLst><a:bgFillStyleLst>'
         + '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>' * 3 +
         '</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>')

MASTER = (HEAD + f'<p:sldMaster {P_NS}><p:cSld><p:bg><p:bgRef idx="1001"><a:schemeClr val="bg1"/></p:bgRef></p:bg>'
          + EMPTY_TREE[len('<p:cSld>'):-len('</p:cSld>')] + '</p:cSld>'
          '<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" '
          'accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" '
          'folHlink="folHlink"/><p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst>'
          '<p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></p:sldMaster>')

LAYOUT = (HEAD + f'<p:sldLayout {P_NS} type="blank" preserve="1">'
          + EMPTY_TREE.replace('<p:cSld>', '<p:cSld name="Blank">') +
          '<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>')


def render_pptx(slides, path, title="Board", link_color=(0.02, 0.39, 0.76)):
    """slides: SlideCanvas objects, one slide each, in order. Links take link_color (PowerPoint colors
    links from the theme, so this keeps them readable on a dark board)."""
    n = len(slides)
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    slide_ct = "".join(f'<Override PartName="/ppt/slides/slide{i}.xml" '
                       'ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>'
                       for i in range(1, n + 1))
    ctypes = (HEAD + '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
              '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
              '<Default Extension="xml" ContentType="application/xml"/>'
              '<Default Extension="png" ContentType="image/png"/>'
              '<Default Extension="jpeg" ContentType="image/jpeg"/>'
              '<Override PartName="/ppt/presentation.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>'
              '<Override PartName="/ppt/slideMasters/slideMaster1.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"/>'
              '<Override PartName="/ppt/slideLayouts/slideLayout1.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"/>'
              '<Override PartName="/ppt/theme/theme1.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/>'
              '<Override PartName="/ppt/presProps.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.presentationml.presProps+xml"/>'
              '<Override PartName="/ppt/viewProps.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.presentationml.viewProps+xml"/>'
              '<Override PartName="/ppt/tableStyles.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml"/>'
              f'{slide_ct}'
              '<Override PartName="/docProps/core.xml" '
              'ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>'
              '<Override PartName="/docProps/app.xml" '
              'ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/></Types>')
    pres = (HEAD + f'<p:presentation {P_NS} saveSubsetFonts="1">'
            '<p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>'
            '<p:sldIdLst>' + "".join(f'<p:sldId id="{255 + i}" r:id="rId{i + 1}"/>' for i in range(1, n + 1)) +
            f'</p:sldIdLst><p:sldSz cx="{CX}" cy="{CY}"/><p:notesSz cx="6858000" cy="9144000"/></p:presentation>')
    pres_rels = rels([("rId1", "slideMaster", "slideMasters/slideMaster1.xml")]
                     + [(f"rId{i + 1}", "slide", f"slides/slide{i}.xml") for i in range(1, n + 1)]
                     + [(f"rId{n + 2}", "theme", "theme/theme1.xml"), (f"rId{n + 3}", "presProps", "presProps.xml"),
                        (f"rId{n + 4}", "viewProps", "viewProps.xml"), (f"rId{n + 5}", "tableStyles", "tableStyles.xml")])
    core = (HEAD + '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" '
            'xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" '
            'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">'
            f'<dc:title>{escape(title)}</dc:title><dc:creator>Chalkboard</dc:creator>'
            f'<dcterms:created xsi:type="dcterms:W3CDTF">{stamp}</dcterms:created>'
            f'<dcterms:modified xsi:type="dcterms:W3CDTF">{stamp}</dcterms:modified></cp:coreProperties>')
    app = (HEAD + '<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties">'
           f'<Application>Chalkboard</Application><Slides>{n}</Slides></Properties>')
    import zipfile  # here, not at the top: on Python 3.12 it loads pathlib, which loads urllib.parse
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ctypes)
        z.writestr("_rels/.rels", rels([("rId1", "officeDocument", "ppt/presentation.xml"),
                                        ("rId2", CORE_REL, "docProps/core.xml"),
                                        ("rId3", "extended-properties", "docProps/app.xml")]))
        z.writestr("docProps/core.xml", core)
        z.writestr("docProps/app.xml", app)
        z.writestr("ppt/presentation.xml", pres)
        z.writestr("ppt/_rels/presentation.xml.rels", pres_rels)
        z.writestr("ppt/presProps.xml", HEAD + f"<p:presentationPr {P_NS}/>")
        z.writestr("ppt/viewProps.xml", HEAD + f"<p:viewPr {P_NS}/>")
        z.writestr("ppt/tableStyles.xml", HEAD + '<a:tblStyleLst xmlns:a="http://schemas.openxmlformats.org/'
                   'drawingml/2006/main" def="{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"/>')
        z.writestr("ppt/theme/theme1.xml", THEME.replace("LINK", _hex(link_color)))
        z.writestr("ppt/slideMasters/slideMaster1.xml", MASTER)
        z.writestr("ppt/slideMasters/_rels/slideMaster1.xml.rels",
                   rels([("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
                         ("rId2", "theme", "../theme/theme1.xml")]))
        z.writestr("ppt/slideLayouts/slideLayout1.xml", LAYOUT)
        z.writestr("ppt/slideLayouts/_rels/slideLayout1.xml.rels",
                   rels([("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")]))
        for i, sl in enumerate(slides, 1):
            items = [("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml")]
            for j, (data, ext) in enumerate(sl.media, 1):
                z.writestr(f"ppt/media/s{i}img{j}.{ext}", data, compress_type=zipfile.ZIP_STORED)
                items.append((f"rIdImg{j}", "image", f"../media/s{i}img{j}.{ext}"))
            z.writestr(f"ppt/slides/slide{i}.xml", sl.xml())
            body = rels(items)
            if sl.links:  # web links point outside the file
                extra = "".join(f'<Relationship Id="rIdLink{j}" Type="{REL}/hyperlink" Target="{escape(u)}" '
                                'TargetMode="External"/>' for j, u in enumerate(sl.links, 1))
                body = body.replace("</Relationships>", extra + "</Relationships>")
            z.writestr(f"ppt/slides/_rels/slide{i}.xml.rels", body)
