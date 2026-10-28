#!/usr/bin/env node
// Generates qml/Icons.js from Lucide (https://lucide.dev, ISC) SVGs.
// Each icon's elements are flattened into one SVG path string for PathSvg.
//
//   npm pack lucide-static && tar xzf lucide-static-*.tgz
//   node scripts/gen-icons.js package/icons
'use strict';
const fs = require('fs');
const path = require('path');

// drav icon name -> Lucide icon name.
const MAP = {
    select: 'mouse-pointer-2',
    frame: 'frame',
    rect: 'square',
    ellipse: 'circle',
    line: 'slash',
    arrow: 'move-up-right',
    polygon: 'triangle',
    star: 'star',
    pen: 'pen-tool',
    pencil: 'pencil',
    text: 'type',
    hand: 'hand',
    image: 'image',
    component: 'component',
    instance: 'diamond',
    group: 'group',
    vector: 'spline',
    boolean: 'squares-unite',
    eye: 'eye',
    eyeOff: 'eye-off',
    lock: 'lock',
    unlock: 'lock-open',
    plus: 'plus',
    minus: 'minus',
    chevronRight: 'chevron-right',
    chevronDown: 'chevron-down',
    menu: 'menu',
    alignLeft: 'align-start-vertical',
    alignHCenter: 'align-center-vertical',
    alignRight: 'align-end-vertical',
    alignTop: 'align-start-horizontal',
    alignVCenter: 'align-center-horizontal',
    alignBottom: 'align-end-horizontal',
    distH: 'align-horizontal-distribute-center',
    distV: 'align-vertical-distribute-center',
    layoutV: 'arrow-down',
    layoutH: 'arrow-right',
    wrap: 'wrap-text',
    clip: 'crop',
    link: 'link',
    rotate: 'rotate-cw',
    corner: 'square-round-corner',
    corners: 'scan',
    strokeIcon: 'square-square',
    effects: 'droplet',
    textLeft: 'text-align-start',
    textCenter: 'text-align-center',
    textRight: 'text-align-end',
    textJustify: 'text-align-justify',
    textTop: 'arrow-up-to-line',
    textMiddle: 'fold-vertical',
    textBottom: 'arrow-down-to-line',
    autoWidth: 'move-horizontal',
    autoHeight: 'move-vertical',
    fixedSize: 'square-dashed',
    swap: 'arrow-down-up',
    detach: 'unlink',
    reset: 'rotate-ccw',
    target: 'crosshair',
    union: 'squares-unite',
    subtract: 'squares-subtract',
    intersect: 'squares-intersect',
    exclude: 'squares-exclude',
    grid: 'grid-3x3',
    close: 'x',
    check: 'check',
    settings: 'sliders-horizontal',
    page: 'file',
    mask: 'blend',
    padH: 'align-horizontal-space-around',
    padV: 'align-vertical-space-around',
    spacingH: 'align-horizontal-space-between',
    underline: 'underline',
    strike: 'strikethrough',
    italic: 'italic',
    truncate: 'ellipsis',
    spacingV: 'align-vertical-space-between',
    variable: 'hexagon',
    style: 'swatch-book',
    search: 'search',
    pipette: 'pipette',
    ruler: 'ruler',
    scale: 'scaling',
    history: 'history',
    listBullet: 'list',
    listOrdered: 'list-ordered',
    adjust: 'sliders-vertical',
    section: 'square-dashed-top-solid',
    gridLayout: 'layout-grid',
    code: 'code',
    terminal: 'square-terminal',
    library: 'library',
    copy: 'copy',
    trash: 'trash-2',
    brush: 'brush',
    bucket: 'paint-bucket',
    textPath: 'route',
    replace: 'replace',
    caseSensitive: 'case-sensitive',
    bold: 'bold',
    play: 'play',
    folder: 'folder-open',
    save: 'save',
    toggle: 'toggle-left',
    measure: 'ruler-dimension-line',
    refresh: 'refresh-cw',
    download: 'download',
    upload: 'upload',
    contrast: 'contrast',
    warning: 'triangle-alert',
    external: 'external-link',
    textProp: 'text-cursor-input',
    branch: 'waypoints',
    points: 'spline-pointer',
    dot: 'circle-dot',
};

const num = v => String(+(+v).toFixed(3));
const NUM = '[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:e[-+]?\\d+)?';

function attrs(s) {
    const o = {};
    s.replace(/([\w-]+)="([^"]*)"/g, (_, k, v) => { o[k] = v; });
    return o;
}

// Re-emits path data with explicit separators (Lucide packs arc flags, e.g. "a1 1 0 011.7 0",
// which Qt's parser misreads) and an absolute first moveto, since paths are joined into one.
function normalize(d) {
    const ARGS = { m: 2, l: 2, h: 1, v: 1, c: 6, s: 4, q: 4, t: 2, a: 7, z: 0 };
    const tok = new RegExp(`\\s*,?\\s*(?:([MmLlHhVvCcSsQqTtAaZz])|(${NUM}))`, 'y');
    const flag = /\s*,?\s*([01])/y;
    const out = [];
    let cmd = '', i = 0, first = true;
    while (i < d.length && /\S/.test(d.slice(i))) {
        tok.lastIndex = i;
        let m = tok.exec(d);
        if (!m) throw new Error(`bad path data at ${i}: ${d}`);
        if (m[1]) {
            cmd = m[1]; i = tok.lastIndex;
            if (cmd === 'z' || cmd === 'Z') { out.push('Z'); continue; }
        }
        let c = cmd;
        if (first) { if (c !== 'm' && c !== 'M') throw new Error(`path must start with moveto: ${d}`); c = 'M'; first = false; }
        const args = [];
        for (let k = 0; k < ARGS[cmd.toLowerCase()]; k++) {
            const re = (cmd === 'a' || cmd === 'A') && (k === 3 || k === 4) ? flag : tok;
            re.lastIndex = i;
            m = re.exec(d);
            const v = re === flag ? m && m[1] : m && m[2];
            if (v == null) throw new Error(`bad args for ${cmd} at ${i}: ${d}`);
            args.push(re === flag ? v : num(v));
            i = re.lastIndex;
        }
        out.push(c + args.join(' '));
        if (cmd === 'm') cmd = 'l'; else if (cmd === 'M') cmd = 'L';
    }
    return out.join(' ');
}

function points(p) {
    const a = p.trim().split(/[\s,]+/);
    const r = [];
    for (let i = 0; i < a.length; i += 2) r.push(`${num(a[i])} ${num(a[i + 1])}`);
    return r;
}

function convert(svg, name) {
    const out = [];
    svg.replace(/<!--[\s\S]*?-->/g, '').replace(/<svg[^>]*>/, '')
        .replace(/<(\w+)\b([^>]*?)\/?>/g, (_, tag, rest) => {
            const a = attrs(rest);
            if (a.fill && a.fill !== 'none') throw new Error(`${name}: filled <${tag}> not supported`);
            switch (tag) {
            case 'path': out.push(normalize(a.d)); break;
            case 'line': out.push(`M${num(a.x1)} ${num(a.y1)} L${num(a.x2)} ${num(a.y2)}`); break;
            case 'polyline':
            case 'polygon': out.push('M' + points(a.points).join(' L') + (tag === 'polygon' ? ' Z' : '')); break;
            case 'rect': {
                const x = +(a.x || 0), y = +(a.y || 0), w = +a.width, h = +a.height;
                const rx = Math.min(+(a.rx ?? a.ry ?? 0), w / 2), ry = Math.min(+(a.ry ?? a.rx ?? 0), h / 2);
                if (!rx) { out.push(`M${num(x)} ${num(y)} H${num(x + w)} V${num(y + h)} H${num(x)} Z`); break; }
                const arc = (ex, ey) => `A${num(rx)} ${num(ry)} 0 0 1 ${num(ex)} ${num(ey)}`;
                out.push(`M${num(x + rx)} ${num(y)} H${num(x + w - rx)} ${arc(x + w, y + ry)} V${num(y + h - ry)} ` +
                         `${arc(x + w - rx, y + h)} H${num(x + rx)} ${arc(x, y + h - ry)} V${num(y + ry)} ${arc(x + rx, y)} Z`);
                break;
            }
            case 'circle':
            case 'ellipse': {
                const cx = +a.cx, cy = +a.cy, rx = +(a.r ?? a.rx), ry = +(a.r ?? a.ry);
                const arc = ex => `A${num(rx)} ${num(ry)} 0 1 0 ${num(ex)} ${num(cy)}`;
                out.push(`M${num(cx - rx)} ${num(cy)} ${arc(cx + rx)} ${arc(cx - rx)} Z`);
                break;
            }
            case 'svg': break;
            default: throw new Error(`${name}: unsupported <${tag}>`);
            }
        });
    return out.join(' ');
}

const dir = process.argv[2];
if (!dir) { console.error('usage: gen-icons.js <lucide-static/icons dir>'); process.exit(1); }
const version = (/lucide-static v([\d.]+)/.exec(fs.readFileSync(path.join(dir, 'x.svg'), 'utf8')) || [])[1] || '?';
const lines = Object.entries(MAP).map(([k, v]) =>
    `    ${k}: "${convert(fs.readFileSync(path.join(dir, `${v}.svg`), 'utf8'), v)}"`);

fs.writeFileSync(path.join(__dirname, '..', 'qml', 'Icons.js'), `// 24x24 outline icon paths (stroke-rendered), generated by scripts/gen-icons.js.
// From Lucide v${version} (https://lucide.dev). Do not edit by hand.
//
// ISC License. Copyright (c) Lucide Icons and Contributors.
// Permission to use, copy, modify, and/or distribute this software for any purpose with or
// without fee is hereby granted, provided that the above copyright notice and this permission
// notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
// WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
// CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
.pragma library

var paths = {
${lines.join(',\n')}
};
`);
console.log(`wrote ${lines.length} icons`);
