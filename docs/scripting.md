# Scripting and plugins

**Plugins → Script console…** runs JavaScript against the open document. Each run is one undo step.
A script that throws is rolled back; one that runs longer than 10 seconds is stopped and rolled back.

The engine is a bare `QJSEngine`: no network, no file system, no timers. Scripts only see `drav`.

## API

| Call | Does |
| --- | --- |
| `drav.selection()` | Selected layer ids |
| `drav.select(ids)` | Replaces the selection |
| `drav.page()`, `drav.pages()` | Current page id, all page ids |
| `drav.children(id)`, `drav.parent(id)` | Tree navigation |
| `drav.get(id)` | Layer properties as an object (same keys as the inspector: `name`, `type`, `x`, `y`, `w`, `h`, `rotation`, `opacity`, `fills`, `characters`, ...) |
| `drav.set(id, key, value)` | Sets one property, same keys as `get` |
| `drav.create(type, parent, props)` | Creates `"FRAME"`, `"RECTANGLE"`, `"ELLIPSE"`, `"TEXT"`, ...; returns its id. `parent` 0 means the current page |
| `drav.remove(id)` | Deletes a layer |
| `drav.find(name)` | Ids of layers on the current page whose name contains `name` (case-insensitive) |
| `drav.notify(message)` | Shows a message in the status bar |
| `print(...)` | Writes to the console output |

The script's last expression value is printed too.

## Plugins

A plugin is a `.js` file in the plugins folder (**Plugins → Open plugins folder**). It shows up in
the Plugins menu. The first lines may name it:

```js
// name: Tidy into a grid
// description: Arranges the selected layers in a grid.
```

Examples: [`examples/plugins`](../examples/plugins).
