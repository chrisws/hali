# Graph Tree View JSON Format

# Graph Tree View JSON Format

This document describes the JSON format for creating tree-view graphs with nested children in Hali.

## Overview

The tree view supports unlimited nesting depth. Each node can have child nodes, which can themselves have children.

## Required Fields

| Field       | Type   | Description                                       |
|-------------|--------|---------------------------------------------------|
| `title`     | string | Title of the graph (optional)                     |
| `type`      | string | Must be either `"tree"` or `"bar"`                |
| `suffix`    | string | Unit label (optional)                             |
| `precision` | int    | display precision for value (optional)            |
| `percent`   | bool   | When true display bar chart values as percentages |
| `data`      | array  | Root-level nodes array                            |

## Node Structure

Each node in the `data` array must have:

| Field | Type | Description |
|-------|------|-------------|
| `label` | string | Node label/name |
| `value` | float | Numeric value for the node |
| `children` | array | Optional array of child nodes |

## Example bar

```json
{
   "type": "bar",
   "title": "KV Cache Usage",
   "data": [
     {"label": "A", "value": 85},
     {"label": "B", "value": 72},
     {"label": "C", "value": 91}
   ]
}
```

## Example tree

```
{
  "title": "Project Structure",
  "type": "tree",
  "data": [{
    "label": "level 1.1",
    "children": [{
      "label": "level 2.1",
      "value": 111,
      "children": [{
        "label": "level 3.1",
        "value": 222.3
      }, {
        "label": "level 3.2",
        "value": 333.3
      }]
    }, {
      "label": "level 2.2",
      "value": 444.2,
      "children": [{
        "label": "level 3.1",
        "value": 555.3
        }
      ]}
    ]}, {
      "label": "level 1.2",
      "value": 666.1,
      "children": [{
        "label": "2.1",
        "value": 777.2
      },{
        "label": "2.2",
        "value": 888.2
      }]
  }]
}
  
```

## Usage

TOOL:GRAPH `json_string`
HALI_END_TOOL

The `json_string` should be a valid JSON string matching the format above.

**Important:** The JSON must be passed as a properly formatted string. When calling TOOL:GRAPH, ensure the JSON is:
- Valid JSON syntax
- Properly escaped if needed
- Not wrapped in additional JSON object syntax

### Correct Example

```json
{"title":"Demo","type":"tree","data":[{"label":"Root","value":100.0,"children":[{"label":"Child","value":50.0,"children":[{"label":"Leaf","value":25.0}]}]}]}
```

### Common Mistakes

- ❌ Not providing the final closing `}` character
- ❌ Passing a JSON object instead of a string
- ❌ Using unescaped special characters
- ❌ Missing required fields like `type` or `data`
- ❌ Incorrect nesting (using `data` instead of `children` for nested nodes)

## Notes

- The `data` field at the root level contains top-level nodes
- Nested nodes use the `children` field instead of `data`
