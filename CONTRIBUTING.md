# Contributing to StreamLumo Engine

Thank you for your interest in contributing to StreamLumo Engine! This document provides guidelines for contributing to the project.

## Code of Conduct

Please be respectful and constructive in all interactions. We're building something together.

## License

By contributing to StreamLumo Engine, you agree that your contributions will be licensed under the **GPL-2.0** license.

## Getting Started

### Prerequisites

- CMake 3.22+
- Ninja build system
- C++17 compatible compiler
- OBS Studio source and dependencies

### Setting Up Development Environment

1. Fork and clone the repository:
   ```bash
   git clone https://github.com/YOUR_USERNAME/streamlumo-engine.git
   cd streamlumo-engine
   ```

2. Set up OBS Studio dependencies:
   ```bash
   ./scripts/download-deps.sh
   ```

3. Build OBS Studio:
   ```bash
   ./scripts/build-obs.sh
   ```

4. Build StreamLumo Engine:
   ```bash
   cmake --preset macos-arm64  # or your platform
   cmake --build --preset macos-arm64
   ```

## How to Contribute

### Reporting Bugs

- Check existing issues first
- Use the bug report template
- Include:
  - OS and version
  - Steps to reproduce
  - Expected vs actual behavior
  - Logs if available

### Suggesting Features

- Check existing issues/discussions
- Explain the use case
- Describe the expected behavior

### Submitting Code

1. **Fork** the repository
2. **Create a branch** from `main`:
   ```bash
   git checkout -b feature/my-feature
   ```
3. **Make your changes**
4. **Test** your changes
5. **Commit** with clear messages:
   ```bash
   git commit -m "feat: Add new feature X"
   ```
6. **Push** to your fork
7. **Open a Pull Request**

### Commit Message Format

We use conventional commits:

- `feat:` New feature
- `fix:` Bug fix
- `docs:` Documentation
- `refactor:` Code refactoring
- `test:` Adding tests
- `chore:` Maintenance

Examples:
```
feat: Add Linux ARM64 platform support
fix: Resolve WebSocket connection timeout
docs: Update build instructions for Windows
```

## Code Style

### C++ Guidelines

- Use C++17 features
- Follow existing code style
- Use meaningful variable names
- Add comments for complex logic
- Keep functions focused and small

### File Organization

```
src/
├── main.cpp           # Entry point
├── engine.cpp/h       # Core OBS initialization
├── config.cpp/h       # Configuration handling
├── logging.cpp/h      # Logging utilities
├── frontend-stubs.cpp/h # Headless frontend API
└── platform/          # Platform-specific code
    ├── platform.h
    ├── platform_macos.cpp
    ├── platform_windows.cpp
    └── platform_linux.cpp
```

## Testing

### Manual Testing

1. Build the engine
2. Run with different options:
   ```bash
   ./streamlumo-engine --port 4455 --log-level debug
   ```
3. Connect with obs-websocket client
4. Test core functionality

### WebSocket Testing

```javascript
const OBSWebSocket = require('obs-websocket-js').default;
const obs = new OBSWebSocket();

await obs.connect('ws://127.0.0.1:4455');
console.log(await obs.call('GetVersion'));
console.log(await obs.call('GetSceneList'));
```

## Pull Request Process

1. Update documentation if needed
2. Add yourself to CONTRIBUTORS if first contribution
3. Ensure CI passes
4. Request review from maintainers
5. Address feedback
6. Squash commits if requested

## Questions?

- Open a GitHub Discussion
- Check existing issues
- Read the documentation

Thank you for contributing! 🎉
