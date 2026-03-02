# Standard Library: File System (`std/fs.zc`)

The `fs` module provides functionality for interacting with the file system, including file I/O and directory operations.

## Usage

```zc
import "std/fs.zc"

fn main() {
    // Reading a file
    let res = File::open("example.txt", "r");
    if (res.is_ok()) {
        let file = res.unwrap();
        let content = file.read_to_string();
        if (content.is_ok()) {
            let s = content.unwrap();
            println "{s.c_str()}";
        }
        file.close();
    }

    // Static utilities
    if (File::exists("data")) {
        println "Data directory exists";
    }
}
```

## Structs

### File

Represents an open file handle.

```zc
struct File {
    handle: void*;
}
```

### Metadata

File or directory metadata.

```zc
struct Metadata {
    size: U64;
    is_dir: bool;
    is_file: bool;
}
```

### DirEntry

Represents an entry in a directory.

```zc
struct DirEntry {
    name: String;
    is_dir: bool;
}
```

## File Methods

### Open / Close

| Method | Signature | Description |
| :--- | :--- | :--- |
| **open** | `File::open(path: char*, mode: char*) -> Result<File>` | Opens a file with the specified mode (e.g., "r", "w", "rb"). Returns a `Result`. |
| **close** | `close(self)` | Closes the file handle. |

### Read / Write

| Method | Signature | Description |
| :--- | :--- | :--- |
| **read_to_string** | `read_to_string(self) -> Result<String>` | Reads the entire file content into a String. |
| **read_all** | `File::read_all(path: char*) -> Result<String>` | Static utility to open, read, and close a file in one go. |
| **read_lines** | `File::read_lines(path: char*) -> Result<Vec<String>>` | Static utility to read a file entirely into an array of split line Strings. |
| **write_string** | `write_string(self, content: char*) -> Result<bool>` | Writes a single string to the file. |
| **write_lines** | `File::write_lines(path: char*, lines: Vec<String>*) -> Result<bool>` | Static utility to sequentially write an array of String lines to a file separated by newlines. |

## Static Utilities

| Method | Signature | Description |
| :--- | :--- | :--- |
| **exists** | `File::exists(path: char*) -> bool` | Checks if a path exists. |
| **metadata** | `File::metadata(path: char*) -> Result<Metadata>` | Retrieves metadata for a path. |
| **create_dir** | `File::create_dir(path: char*) -> Result<bool>` | Creates a new directory. |
| **remove_file** | `File::remove_file(path: char*) -> Result<bool>` | Deletes a file. |
| **remove_dir** | `File::remove_dir(path: char*) -> Result<bool>` | Deletes a directory. |
| **read_dir** | `File::read_dir(path: char*) -> Result<Vec<DirEntry>>` | Reads the contents of a directory. Returns a vector of `DirEntry`. |
