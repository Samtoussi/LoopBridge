# LoopBridge development rules

The latest working source files are the source of truth.

- Follow YAGNI and prioritize practical requirements.
- Preserve existing functionality and architecture.
- Reuse existing JUCE functionality whenever possible.
- Avoid unnecessary dependencies, abstractions and refactoring.
- Read relevant source files before modifying them.
- Prefer small, cohesive changes over large rewrites.
- Never modify the audio engine, BPM handling or VST bridge unless the task explicitly requires it.
- Keep Windows as the current development platform while preserving future macOS compatibility.
- Never modify credentials.json or commit secrets.
- Build and verify changes whenever practical.
- Do not create Git commits unless explicitly requested.
