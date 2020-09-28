### v1.0.0 2020-09-28

## Overview
This implementation is a subset of the intersection between [JSONPath](https://goessner.net/articles/JsonPath) and [YAML Path](https://pypi.org/project/yamlpath), and it focuses on addressing elements inside YAML files (node descriptors). There are no *query*-like capabilities.

Example YAML structure:
```yaml
foo:
  - bar: &bar True
    first: First Bar
    second: 2
    arr: [1, 2, 3]
  - baz: False
    other_bar: *bar
    first: First Baz
    some.el/here: Delimiters...
    "bar's": 0
```

Example JSON structure:
```json
{
    "foo": [
        {
            "bar": true,
            "first": "First Bar", 
            "second": 2,
            "arr": [1, 2, 3]
        },
        {
            "baz": false,
            "other_bar": true,
            "first": "First Baz",
            "some.el/here": "Delimiters...",
            "bar's": 0,
        }
    ]
}
```

## Delimiters
Dot-notation (`.`) is the only supported notation to define *map key* path segments. Both *map key* and *sequence index* path segments could also be defined using square brackets (`[`, `]`). For *map key* and *map keys selection* segments both single (`'`) and double (`"`) quotes are supported, `['key']` is the same as `["key"]` and `['key',"other's key"]` is a valid path segment.

For example: `$.foo[0].bar` or `.foo[0]['bar']` or `['foo'][0].bar` or `foo[1]["bar's"]`.

## Special symbols
A path might be prefixed by a dollar sign and a dot (`$`, `.`), but this prefix is retained for compatibility with *JSONPath* and not mandatory. Implicit *document root* is assumed unless the path explicitly starts with an *anchor* (`&...`) segment (see below for details).

If the first segment is a *map key* segment (and explicit *document root* is omitted) the initial dot (`.`) is also not mandatory.

For example, these paths are equal: `$.el`, `.el`, `el`, `['el']`. And they all address the value stored in the "el" key of the top-most map of the document.

An asterisk (`*`) as a key name has a special meaning, and treated as an all-inclusive *keys selection* section (see below). That's it, `$.*` expression would include all keys of the map in the document root. The `[*]` syntax is also valid. One should use the `[:]` notation to acheive same effect for sequences (include all indices).

## Path Segment Types

#### Document Root
`$`

Optional explicit document root. Only allowed to appear at the beginning of the path.

```python
$.foo[0].bar = .foo[0].bar = foo[0].bar
== true
```


#### Map Key
`.map.key` or `.map['key']`

```python
$.foo[0].second = ['foo'][0]['second']
== 2
```


#### Map Keys Selection
`.map['key1','key2',...'keyN']`

Special syntax for the all-inclusive key selection: `.*`. Also, there is the `[*]` variant of this syntax.

```python
$.foo[0]['first','second'] = ['foo'][0]['first','second']
== {"first": "First Bar", "second": 2}

foo[0]['first','second','bar','arr'] = foo[0].*
== {"bar": true, "first": "First Bar", "second": 2, "arr": [1, 2, 3]}
```


#### Sequence Index
`.array[<zero or positive number>]`

```python
$.foo[0]
== {"bar": True, "first": "First Bar", "second": 2}
```


#### Sequence Indices Set
`.array[<zero or positive number>,<zero or positive number>,...<zero or positive number>]`

Special syntax for the all-inclusive indices set: `[:]`.

```python
$.foo[0].arr[0,1,2] = foo[0].arr[:]
== [1, 2, 3]
```


#### Anchor
`&anchor`

Matches elements starting from the given anchor instead of the document root. This segment is only sensible in paths for YAML documents as there is no anchors/aliases concept in the JSON specification.

```python
$.foo[0].bar = &bar
== True
```