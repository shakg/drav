#include "Editor.h"

namespace drav {

namespace {

SkColor4f hex(unsigned rgb, float a = 1)
{
    return {((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, a};
}

Props text(const QString& s, float size, int weight, SkColor4f color)
{
    Props p;
    p.type = NodeType::Text;
    p.name = s.left(30);
    p.text = s;
    p.textStyle.size = size;
    p.textStyle.weight = weight;
    p.fills = {Paint::solid(color)};
    p.layoutChild.sizingH = LayoutSizing::Hug;
    p.layoutChild.sizingV = LayoutSizing::Hug;
    return p;
}

Props frame(const QString& name, float w, float h, SkColor4f fill)
{
    Props p;
    p.type = NodeType::Frame;
    p.name = name;
    p.width = w;
    p.height = h;
    p.fills = {Paint::solid(fill)};
    return p;
}

void autoLayout(Props& p, LayoutMode mode, float spacing, float padH, float padV, bool hug = true)
{
    p.layout.mode = mode;
    p.layout.spacing = spacing;
    p.layout.padL = p.layout.padR = padH;
    p.layout.padT = p.layout.padB = padV;
    if (hug) {
        p.layoutChild.sizingH = LayoutSizing::Hug;
        p.layoutChild.sizingV = LayoutSizing::Hug;
    }
}

} // namespace

// Sample file showing the main features; also used by automated screenshots.
void Editor::loadDemo()
{
    newDocument();
    Document& d = *m_doc;
    const NodeId page = d.currentPage();
    d.begin(tr("Demo"));
    d.edit(page).name = QStringLiteral("Showcase");

    // ---- Button component set with two variants ----
    auto makeButton = [&](const QString& name, SkColor4f bg, SkColor4f fg) {
        Props b = frame(name, 10, 10, bg);
        b.type = NodeType::Component;
        autoLayout(b, LayoutMode::Horizontal, 8, 20, 12);
        b.layout.counterAlign = LayoutAlign::Center;
        b.corners = {8, 8, 8, 8};
        NodeId id = d.create(b, page);
        d.create(text(QStringLiteral("Get started"), 14, 600, fg), id);
        return id;
    };
    NodeId primary = makeButton(QStringLiteral("Variant=Primary"), hex(0x0D99FF), SkColors::kWhite);
    NodeId secondary = makeButton(QStringLiteral("Variant=Secondary"), hex(0xEDEDED), hex(0x1E1E1E));
    d.edit(primary).variantProps = {{QStringLiteral("Variant"), QStringLiteral("Primary")}};
    d.edit(secondary).variantProps = {{QStringLiteral("Variant"), QStringLiteral("Secondary")}};
    Props set = frame(QStringLiteral("Button"), 10, 10, hex(0xFFFFFF, 0));
    set.type = NodeType::ComponentSet;
    set.fills.clear();
    autoLayout(set, LayoutMode::Horizontal, 20, 20, 20);
    set.strokes = {Paint::solid(hex(0x9747FF))};
    set.stroke.dashes = {10, 5};
    set.corners = {5, 5, 5, 5};
    NodeId setId = d.create(set, page);
    d.setPosition(setId, 0, -140);
    d.move(primary, setId);
    d.move(secondary, setId);

    // ---- Card built with nested auto layout ----
    Props card = frame(QStringLiteral("Card"), 360, 10, SkColors::kWhite);
    autoLayout(card, LayoutMode::Vertical, 14, 20, 20, false);
    card.layoutChild.sizingV = LayoutSizing::Hug;
    card.corners = {16, 16, 16, 16};
    card.cornerSmoothing = 0.6f;
    Effect shadow;
    shadow.color = hex(0x000000, 0.12f);
    shadow.offset = {0, 8};
    shadow.radius = 24;
    card.effects = {shadow};
    NodeId cardId = d.create(card, page);

    Props hero;
    hero.type = NodeType::Rectangle;
    hero.name = QStringLiteral("Hero");
    hero.height = 170;
    hero.corners = {10, 10, 10, 10};
    hero.layoutChild.sizingH = LayoutSizing::Fill;
    Paint grad;
    grad.type = PaintType::LinearGradient;
    grad.gradStart = {0, 0};
    grad.gradEnd = {1, 1};
    grad.stops = {{0, hex(0x7B61FF)}, {0.5f, hex(0x0D99FF)}, {1, hex(0x14AE5C)}};
    hero.fills = {grad};
    d.create(hero, cardId);

    d.create(text(QStringLiteral("Offline vector design"), 22, 700, hex(0x1E1E1E)), cardId);
    Props body = text(QStringLiteral("Auto layout, components, variants, boolean operations, "
                                     "effects and a pen tool. Everything stays on your machine."),
        14, 400, hex(0x5E5E5E));
    body.textStyle.autoResize = TextAutoResize::Height;
    body.textStyle.lineHeight = 150;
    body.textStyle.lineHeightPercent = true;
    body.layoutChild.sizingH = LayoutSizing::Fill;
    d.create(body, cardId);

    Props row = frame(QStringLiteral("Actions"), 10, 10, SkColors::kWhite);
    row.fills.clear();
    autoLayout(row, LayoutMode::Horizontal, 10, 0, 0);
    NodeId rowId = d.create(row, cardId);
    Props inst;
    inst.type = NodeType::Instance;
    inst.componentId = primary;
    inst.name = QStringLiteral("Button");
    d.create(inst, rowId);
    inst.componentId = secondary;
    NodeId second = d.create(inst, rowId);
    d.updateDerived();
    // Instance override: different label.
    d.edit(d.at(second).children.front()).text = QStringLiteral("Learn more");
    d.setPosition(cardId, 0, 0);

    // ---- Shapes ----
    Props star;
    star.type = NodeType::Star;
    star.name = QStringLiteral("Star");
    star.width = star.height = 120;
    Paint radial;
    radial.type = PaintType::RadialGradient;
    radial.gradStart = {0.5f, 0.5f};
    radial.gradEnd = {1, 0.5f};
    radial.stops = {{0, hex(0xFFCD29)}, {1, hex(0xF24822)}};
    star.fills = {radial};
    NodeId starId = d.create(star, page);
    d.setPosition(starId, 420, 0);

    Props donut;
    donut.type = NodeType::Ellipse;
    donut.name = QStringLiteral("Donut");
    donut.width = donut.height = 120;
    donut.arcSweep = 270;
    donut.arcInner = 0.55f;
    Paint ang;
    ang.type = PaintType::AngularGradient;
    ang.gradStart = {0.5f, 0.5f};
    ang.gradEnd = {1, 0.5f};
    ang.stops = {{0, hex(0x0D99FF)}, {1, hex(0x9747FF)}};
    donut.fills = {ang};
    NodeId donutId = d.create(donut, page);
    d.setPosition(donutId, 570, 0);

    Props boolp;
    boolp.type = NodeType::BooleanOp;
    boolp.name = QStringLiteral("Subtract");
    boolp.boolOp = BooleanOpType::Subtract;
    boolp.fills = {Paint::solid(hex(0x14AE5C))};
    NodeId boolId = d.create(boolp, page);
    Props sq;
    sq.type = NodeType::Rectangle;
    sq.width = sq.height = 110;
    sq.corners = {24, 24, 24, 24};
    d.create(sq, boolId);
    Props hole;
    hole.type = NodeType::Ellipse;
    hole.width = hole.height = 70;
    NodeId holeId = d.create(hole, boolId);
    d.setPosition(holeId, 55, 55);
    d.setPosition(boolId, 420, 160);

    Props wave;
    wave.type = NodeType::Vector;
    wave.name = QStringLiteral("Wave");
    Contour c;
    c.pts = {{{0, 40}, {0, 40}, {40, -20}, AnchorMode::Asymmetric},
        {{100, 40}, {60, 100}, {140, -20}, AnchorMode::Mirrored},
        {{200, 40}, {160, 100}, {200, 40}, AnchorMode::Asymmetric}};
    wave.vector.contours = {c};
    wave.width = 200;
    wave.height = 80;
    wave.strokes = {Paint::solid(hex(0x9747FF))};
    wave.stroke.weight = 4;
    wave.stroke.align = StrokeAlign::Center;
    wave.stroke.startCap = wave.stroke.endCap = StrokeCap::Round;
    NodeId waveId = d.create(wave, page);
    d.setPosition(waveId, 560, 180);

    Props arrow;
    arrow.type = NodeType::Line;
    arrow.name = QStringLiteral("Arrow");
    arrow.width = 180;
    arrow.height = 0;
    arrow.strokes = {Paint::solid(hex(0xF5F5F5))};
    arrow.stroke.weight = 2;
    arrow.stroke.endCap = StrokeCap::TriangleArrow;
    NodeId arrowId = d.create(arrow, page);
    d.setPosition(arrowId, 420, 320);

    // ---- Wrapping grid of swatches ----
    Props grid = frame(QStringLiteral("Palette"), 330, 10, hex(0x2C2C2C));
    autoLayout(grid, LayoutMode::Horizontal, 10, 16, 16, false);
    grid.layout.wrap = true;
    grid.layout.counterSpacing = 10;
    grid.layoutChild.sizingV = LayoutSizing::Hug;
    grid.corners = {12, 12, 12, 12};
    NodeId gridId = d.create(grid, page);
    const unsigned colors[] = {0xF24822, 0xFFA629, 0xFFCD29, 0x14AE5C, 0x0D99FF, 0x9747FF, 0xFF24BD, 0xE6E6E6, 0x1E1E1E, 0x667799};
    for (unsigned col : colors) {
        Props sw;
        sw.type = NodeType::Rectangle;
        sw.name = QStringLiteral("Swatch");
        sw.width = sw.height = 50;
        sw.corners = {8, 8, 8, 8};
        sw.fills = {Paint::solid(hex(col))};
        d.create(sw, gridId);
    }
    d.setPosition(gridId, 420, 370);

    d.commit();
    d.markSaved();
    zoomToFit();
}

} // namespace drav
