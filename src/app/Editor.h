#pragma once

#include "core/Document.h"
#include "render/Renderer.h"

#include <include/core/SkPicture.h>

#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <map>
#include <memory>

class QKeyEvent;
class QInputMethodEvent;
class SkCanvas;

namespace drav {

class LayerModel;
class Tool;

struct InputEvent {
    QPointF screen;
    SkPoint page;
    Qt::MouseButtons buttons;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers mods;
};

// What the render thread draws for one frame. Immutable once handed over.
struct FrameSnapshot {
    sk_sp<SkPicture> content; // page space
    sk_sp<SkPicture> overlay; // screen space (logical pixels)
    SkMatrix view;            // page -> logical screen
    SkColor4f background{};
};

struct SnapGuide {
    bool vertical;
    float pos;
    float from, to;
};

// Central editor state shared by the canvas item, tools and the QML panels.
class Editor : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString tool READ toolName WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap props READ props NOTIFY propsChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QObject* layers READ layersObject CONSTANT)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(qint64 currentPage READ currentPage WRITE setCurrentPage NOTIFY pagesChanged)
    Q_PROPERTY(QVariantList components READ components NOTIFY componentsChanged)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY undoChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileChanged)
    Q_PROPERTY(bool outlines READ outlines WRITE setOutlines NOTIFY viewChanged)
    Q_PROPERTY(bool showGrids READ showGrids WRITE setShowGrids NOTIFY viewChanged)
    Q_PROPERTY(bool snapToPixels MEMBER m_snapPixels NOTIFY viewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int cursorShape READ cursorShape NOTIFY cursorChanged)
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY toolChanged)
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(QVariantMap library READ library NOTIFY libraryChanged)
    Q_PROPERTY(bool rulers READ rulers WRITE setRulers NOTIFY viewChanged)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)

public:
    explicit Editor(QObject* parent = nullptr);
    ~Editor() override;

    Document& doc() { return *m_doc; }
    const Document& doc() const { return *m_doc; }
    Renderer& renderer() { return *m_renderer; }

    // ---- view ----
    double zoom() const { return m_zoom; }
    SkMatrix view() const;
    SkPoint toPage(QPointF screen) const;
    QPointF toScreen(SkPoint page) const;
    void setViewportSize(QSizeF s);
    QSizeF viewportSize() const { return m_viewport; }
    void panBy(QPointF delta);
    void zoomAt(QPointF screen, double factor);
    Q_INVOKABLE void zoomTo(double z);
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToFit();
    Q_INVOKABLE void zoomToSelection();
    bool outlines() const { return m_renderer->options().outlines; }
    void setOutlines(bool on);
    bool showGrids() const { return m_renderer->options().showGrids; }
    void setShowGrids(bool on);
    bool rulers() const { return m_rulers; }
    void setRulers(bool on);
    static constexpr float kRulerSize = 18; // logical px

    // ---- frame production (GUI thread) ----
    FrameSnapshot snapshot();
    void requestUpdate() { emit updateRequested(); }
    void reportFrameTime(double ms);
    double fps() const { return m_fps; }

    // ---- input (from the canvas item) ----
    void mousePress(const InputEvent& e);
    void mouseMove(const InputEvent& e);
    void mouseRelease(const InputEvent& e);
    void mouseDoubleClick(const InputEvent& e);
    void hover(const InputEvent& e);
    bool keyPress(QKeyEvent* e);
    bool keyRelease(QKeyEvent* e);
    bool inputMethod(QInputMethodEvent* e);
    void leave();

    // ---- tools ----
    QString toolName() const;
    Q_INVOKABLE void setTool(const QString& name);
    Tool* tool() const { return m_tool; }
    void enterTextEdit(NodeId id, SkPoint pagePos, bool selectAll, bool created = false);
    // Screen-space corners of the selection frame (oriented for a single node). False if empty.
    bool selectionQuad(SkPoint quad[4]) const;
    void enterVectorEdit(NodeId id);
    bool textEditing() const;
    int cursorShape() const;
    void cursorMaybeChanged() { emit cursorChanged(); }

    // ---- hit testing ----
    NodeId hitDeepest(SkPoint page, float tolerance = -1) const;
    NodeId selectableAt(SkPoint page, bool deep) const;
    NodeId frameLabelAt(QPointF screen) const;
    NodeId containerAt(SkPoint page, const std::vector<NodeId>& exclude) const;
    bool isTopLevelContainer(NodeId id) const;
    NodeId hovered() const { return m_hover; }
    void setHovered(NodeId id);

    // ---- selection ----
    int selectionCount() const { return (int)m_doc->selection().size(); }
    void select(std::vector<NodeId> ids);
    SkRect selectionBounds() const; // page space AABB
    std::vector<NodeId> topLevelSelection() const; // drops nodes whose ancestors are selected
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void selectChildren();
    Q_INVOKABLE void selectParent();
    Q_INVOKABLE void selectNode(qint64 id, bool additive);
    Q_INVOKABLE void selectRange(qint64 id);
    // Selects layers on the page like the first selected one. what: "matching" (type, fills,
    // strokes, effects), "fill", "stroke", "effect", "font", "instance" (same main component), "type".
    Q_INVOKABLE void selectSame(const QString& what);
    // Find in text layers (and layer names). Returns matching layer ids on the current page.
    Q_INVOKABLE QVariantList findLayers(const QString& query, bool caseSensitive, bool names) const;
    Q_INVOKABLE int replaceText(const QString& query, const QString& replacement, bool caseSensitive, bool names);
    // Renames the selection. In `pattern`, $& is the current name, $n counts up from `start` and
    // $N counts down; an empty pattern keeps the name. `find` -> `replace` applies afterwards.
    Q_INVOKABLE void renameLayers(const QString& pattern, const QString& find, const QString& replace, int start);

    // ---- overlay state for tools ----
    std::vector<SnapGuide> guides;
    SkRect marquee = SkRect::MakeEmpty();
    NodeId dropTarget = kNoNode;
    bool showSizeLabel = true;

    // ---- properties for the inspector ----
    QVariantMap props() const;
    Q_INVOKABLE void setProp(const QString& key, const QVariant& value);
    // Live edits (slider scrubbing, color drags) fold into one undo step until endLive().
    Q_INVOKABLE void setPropLive(const QString& key, const QVariant& value);
    Q_INVOKABLE void endLive();

    // ---- commands ----
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void pasteInPlace();
    // format: "SVG" (markup as text), "PNG" (2x image) or "CSS".
    Q_INVOKABLE void copyAs(const QString& format);
    // Read-only code for the first selected layer and its direct children ("CSS").
    Q_INVOKABLE QString inspectCode() const;
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void group();
    Q_INVOKABLE void ungroup();
    Q_INVOKABLE void frameSelection();
    Q_INVOKABLE void addAutoLayout();
    Q_INVOKABLE void removeAutoLayout();
    Q_INVOKABLE void createComponent();
    Q_INVOKABLE void combineAsVariants();
    Q_INVOKABLE void detachInstance();
    Q_INVOKABLE void resetOverrides();
    Q_INVOKABLE void goToMainComponent();
    Q_INVOKABLE void swapComponent(qint64 componentId);
    Q_INVOKABLE void setVariant(const QString& prop, const QString& value);
    Q_INVOKABLE void placeComponent(qint64 componentId);
    Q_INVOKABLE void booleanOp(const QString& op);
    Q_INVOKABLE void flatten();
    Q_INVOKABLE void outlineStroke();
    Q_INVOKABLE void useAsMask();
    Q_INVOKABLE void align(const QString& how);
    Q_INVOKABLE void distribute(bool horizontal);
    Q_INVOKABLE void bringForward();
    Q_INVOKABLE void sendBackward();
    Q_INVOKABLE void bringToFront();
    Q_INVOKABLE void sendToBack();
    Q_INVOKABLE void flip(bool horizontal);
    Q_INVOKABLE void toggleVisible();
    Q_INVOKABLE void toggleLocked();
    Q_INVOKABLE void setNodeVisible(qint64 id, bool visible);
    Q_INVOKABLE void setNodeLocked(qint64 id, bool locked);
    Q_INVOKABLE void setNodeExpanded(qint64 id, bool expanded);
    Q_INVOKABLE void renameNode(qint64 id, const QString& name);
    Q_INVOKABLE void moveNodeInTree(qint64 id, qint64 newParent, int index);
    Q_INVOKABLE void nudge(double dx, double dy);
    // Scales the selection (and contents, strokes, effects, text) about its top-left corner.
    Q_INVOKABLE void scaleSelection(double factor);
    void scaleNodes(const std::vector<NodeId>& ids, float factor, SkPoint anchor);
    // Eyedropper: the next canvas click reports a color through colorPicked() instead of filling.
    Q_INVOKABLE void pickColor();
    // Crop the first image fill of the selected layer (switches it to crop mode).
    Q_INVOKABLE void enterCrop();
    bool pickingForPopup() const { return m_pickForPopup; }
    void finishPick(SkColor4f c, bool apply);
    SkColor4f sampleColor(SkPoint page);
    Q_INVOKABLE void rotateSelection(double degrees);

    // ---- pages ----
    QVariantList pages() const;
    qint64 currentPage() const { return qint64(m_doc->currentPage()); }
    void setCurrentPage(qint64 id);
    Q_INVOKABLE void addPage();
    Q_INVOKABLE void renamePage(qint64 id, const QString& name);
    Q_INVOKABLE void deletePage(qint64 id);
    Q_INVOKABLE void duplicatePage(qint64 id);

    QVariantList components() const;
    QStringList fontFamilies() const;

    // ---- styles and variables (EditorLibrary.cpp) ----
    // {styles: [...], collections: [...], variables: [...]} for the asset panels.
    QVariantMap library() const;
    // slot: "fills", "strokes", "text", "effects" or "grids". Creates a style from the first
    // selected layer and applies it to the selection. Returns the style id.
    Q_INVOKABLE QString createStyle(const QString& slot, const QString& name);
    Q_INVOKABLE void applyStyle(const QString& slot, const QString& styleId); // "" detaches
    Q_INVOKABLE void editStyle(const QString& id, const QVariantMap& changes, bool live = false);
    Q_INVOKABLE void updateStyleFromSelection(const QString& id);
    Q_INVOKABLE QString duplicateStyle(const QString& id);
    Q_INVOKABLE void deleteStyle(const QString& id);
    Q_INVOKABLE void selectStyleUsers(const QString& id);
    Q_INVOKABLE QString addCollection(const QString& name);
    Q_INVOKABLE void renameCollection(const QString& id, const QString& name);
    Q_INVOKABLE void deleteCollection(const QString& id);
    Q_INVOKABLE QString addMode(const QString& collection, const QString& name);
    Q_INVOKABLE void renameMode(const QString& collection, const QString& mode, const QString& name);
    Q_INVOKABLE void deleteMode(const QString& collection, const QString& mode);
    // type: "COLOR", "FLOAT", "STRING", "BOOLEAN"
    Q_INVOKABLE QString addVariable(const QString& collection, const QString& type, const QString& name);
    Q_INVOKABLE void renameVariable(const QString& id, const QString& name);
    Q_INVOKABLE void setVariableDescription(const QString& id, const QString& text);
    Q_INVOKABLE void setVariableValue(const QString& id, const QString& mode, const QVariant& value, bool live = false);
    Q_INVOKABLE void setVariableAlias(const QString& id, const QString& mode, const QString& target);
    Q_INVOKABLE void deleteVariable(const QString& id);
    // field: a bindable property key ("spacing", "w", ...) or "fills/N", "strokes/N", "effects/N".
    Q_INVOKABLE void bindVariable(const QString& field, const QString& variableId); // "" unbinds
    Q_INVOKABLE void setVariableMode(const QString& collection, const QString& mode); // "" = inherit
    Q_INVOKABLE QVariant resolveVariable(const QString& id) const; // in the selection's context
    // Variable axes of a font: [{tag, min, max, default}].
    Q_INVOKABLE QVariantList fontAxes(const QString& family, int weight, bool italic) const;
    // Families used in the document that are not installed, and replacing one everywhere.
    Q_INVOKABLE QStringList missingFonts() const;
    Q_INVOKABLE void replaceFont(const QString& from, const QString& to);

    // ---- component properties (EditorComponents.cpp) ----
    // type: "BOOLEAN", "TEXT", "INSTANCE_SWAP". Adds to the selected component or component set.
    Q_INVOKABLE QString addComponentProp(const QString& type, const QString& name);
    // Defines a property on the enclosing component (or its set) from the selected layer's current
    // value and links the layer to it. field: "visible", "characters" or "mainComponent".
    Q_INVOKABLE QString createLinkedProp(const QString& field, const QString& name);
    Q_INVOKABLE void editComponentProp(const QString& id, const QVariantMap& changes); // name, default, preferred
    Q_INVOKABLE void deleteComponentProp(const QString& id);
    Q_INVOKABLE void linkComponentProp(const QString& field, const QString& propId); // "" unlinks
    Q_INVOKABLE void setInstanceProp(const QString& propId, const QVariant& value);

    // ---- files ----
    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool open(const QUrl& url);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    Q_INVOKABLE bool importFile(const QUrl& url);
    Q_INVOKABLE QString addImageFile(const QUrl& url); // stores an image, returns its hash
    Q_INVOKABLE bool exportSelection(const QUrl& url, const QString& format, double scale);
    Q_INVOKABLE QString suggestedExportName(const QString& format) const;
    Q_INVOKABLE void loadDemo();
    bool canUndo() const { return m_doc->canUndo(); }
    bool canRedo() const { return m_doc->canRedo(); }
    bool isDirty() const { return m_doc->isDirty(); }
    QString fileName() const;
    QUrl fileUrl() const { return m_fileUrl; }
    QString status() const { return m_status; }
    void setStatus(const QString& s);

    // ---- data safety (EditorSafety.cpp) ----
    QStringList recentFiles() const;
    Q_INVOKABLE void clearRecentFiles();
    // Unsaved work is written to a recovery file every 30 s; a crash leaves it for the next start.
    Q_INVOKABLE QVariantList recoveryFiles() const; // [{path, original, name, time}]
    Q_INVOKABLE bool recover(const QString& path);
    Q_INVOKABLE void discardRecovery(const QString& path);
    Q_INVOKABLE void discardChanges(); // the user closed without saving: drop this session's recovery file
    void autosaveNow();
    QString recoveryPath() const;
    // Named versions of the document, kept in the app data folder per file.
    Q_INVOKABLE bool saveVersion(const QString& name);
    Q_INVOKABLE QVariantList versions() const; // [{path, name, time}], newest first
    Q_INVOKABLE bool restoreVersion(const QString& path);

    // Places nodes created by tools: parent container for a point.
    NodeId parentForNewNode(SkPoint page) const;
    // Default props for new shapes.
    Props defaultProps(NodeType t) const;
    // Re-parents keeping the world transform.
    void reparentKeepWorld(NodeId id, NodeId newParent, int index);
    float snapValue(float v) const { return m_snapPixels ? std::round(v) : v; }

    QObject* layersObject() const;

signals:
    void updateRequested();
    void toolChanged();
    void viewChanged();
    void propsChanged();
    void selectionChanged();
    void pagesChanged();
    void componentsChanged();
    void undoChanged();
    void fileChanged();
    void statusChanged();
    void cursorChanged();
    void fpsChanged();
    void libraryChanged();
    void requestRename(qint64 id);
    void requestBulkRename();
    void requestFind();
    void colorPicked(const QColor& color);
    void recentFilesChanged();
    void versionsChanged();

private:
    void onDocChanged(bool structure);
    void onSelectionChanged();
    void drawOverlay(SkCanvas* c);
    void drawSelection(SkCanvas* c);
    void drawFrameLabels(SkCanvas* c);
    void drawRulers(SkCanvas* c);
    void drawPageGuides(SkCanvas* c);
    void drawMeasurements(SkCanvas* c);
    int guideAt(QPointF screen) const;
    bool inRuler(QPointF screen, bool* vertical) const;
    std::vector<NodeId> selected() const { return m_doc->selection(); }
    void applyProp(NodeId id, const QString& key, const QVariant& value);
    // While editing text: applies character/paragraph properties to the selected range.
    bool applyTextRange(const QString& key, const QVariant& value);
    void textRangeProps(QVariantMap& out) const;
    void wrapSelection(NodeType type, const QString& label);
    void addAutoLayoutTo(NodeId frame, LayoutMode forced);
    void reorder(int delta, bool toEnd);
    void placeNodes(const std::vector<NodeId>& ids, SkPoint center);
    void detachRefs(const std::function<bool(const QString&)>& gone);

    std::unique_ptr<Document> m_doc;
    std::unique_ptr<Renderer> m_renderer;
    LayerModel* m_layers = nullptr;

    std::map<QString, std::unique_ptr<Tool>> m_tools;
    Tool* m_tool = nullptr;
    QString m_toolName;
    QString m_returnTool; // tool to restore after a temporary one (space = hand)

    double m_zoom = 1;
    QPointF m_pan{0, 0};
    QSizeF m_viewport{800, 600};
    bool m_snapPixels = true;

    sk_sp<SkPicture> m_content;
    bool m_contentDirty = true;
    NodeId m_hover = kNoNode;

    bool m_live = false;
    QString m_liveKey;
    QString m_status;
    QUrl m_fileUrl;
    double m_fps = 0;
    double m_frameAccum = 0;
    int m_frameCount = 0;
    QByteArray m_internalClipboard;
    std::shared_ptr<const Library> m_lastLibrary; // held to detect library changes
    bool m_rulers = false;
    bool m_alt = false; // Alt held: show distances to the hovered layer
    struct GuideDrag {
        bool active = false;
        int index = -1; // existing guide, or -1 for a new one from a ruler
        bool vertical = true;
        float pos = 0;
        bool remove = false; // dropped back on the ruler
    } m_guideDrag;
    int m_guideHover = -1;
    bool m_pickForPopup = false;
    QString m_sessionId; // names this document's recovery file
    void addRecent(const QString& path);
    QString versionsDir() const;
    void newSession();
    QString m_toolBeforePick;
};

} // namespace drav
