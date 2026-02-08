# Architecture: hilbertviz

The following diagram illustrates the flow of data and control from the CLI entry point down to the core algorithms and I/O handlers.

```mermaid
graph TD
    %% CLI Entry
    Main["src/main.c<br/>(Arg Parsing & Config)"]
    
    %% Core Orchestration
    Render["src/render.c<br/>(hv_render_file)<br/>Orchestrates slicing, math, and I/O"]
    Safety["Render Safety Layer<br/>(preflight alias checks + checked output opens)"]
    
    %% Input Handling
    FileIO["src/file_io.c<br/>(HvInputStream)<br/>Validated sliced reads"]
    
    %% Algorithms
    Hilbert["src/hilbert.c<br/>(d2xy & order selection)"]
    Palette["src/palette.c<br/>(Stairwell palette mapping)"]
    
    %% Output Handling
    Image["src/image.c<br/>(Format Dispatcher)"]
    PPM["src/ppm.c<br/>(P6 binary writer)"]
    PNG["src/png_writer.c<br/>(libpng wrapper)"]
    Legend["Legend Writer<br/>(optional sidecar stats)"]
    PngGate["Build/Runtime Gate<br/>(HILBERTVIZ_WITH_PNG + libpng found,<br/>and .png output path)"]
    
    %% External
    LibPNG["libpng"]
    Files["Filesystem Outputs<br/>(pages + optional legend)"]

    %% Flow
    Main -->|HvRenderOptions| Render
    Render -->|Slice/Stream| FileIO
    Render -->|"Coordinate Mapping (x, y)"| Hilbert
    Render -->|Color Mapping| Palette
    Render -->|Preflight + open policy| Safety
    Render -->|Pixel Buffer| Image
    
    Safety -->|Validated page output stream| Image
    Safety -->|Validated legend stream| Legend
    Legend --> Files

    Image -->|ppm or no extension| PPM
    Image -.->|png path| PNG
    PngGate -.->|enables PNG path| PNG
    PPM --> Files
    PNG --> Files
    PNG -.->|linked when enabled| LibPNG

    %% Styling
    style Main fill:#f9f,stroke:#333
    style Render fill:#bbf,stroke:#333
    style Safety fill:#bbf,stroke:#333
    style Hilbert fill:#bfb,stroke:#333
    style Palette fill:#bfb,stroke:#333
    style FileIO fill:#fbb,stroke:#333
    style Image fill:#fbb,stroke:#333
    style PPM fill:#fbb,stroke:#333
    style PNG fill:#fbb,stroke:#333
    style Legend fill:#fbb,stroke:#333
    style PngGate fill:#ffd,stroke:#333
    style Files fill:#eee,stroke:#333
```

## Notes

- `PNG -> libpng` is conditional: this edge exists only when PNG support is
  enabled at build time (`HILBERTVIZ_WITH_PNG=ON`) and libpng is found.
