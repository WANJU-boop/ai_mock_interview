# Demonstration assets

The PNGs in `assets/` are actual macOS window captures from this application,
using a fictional candidate and Mock providers. No credentials, private resumes,
microphone audio or real candidate data are included.

`mock-walkthrough.mp4` is a 30-second illustrated walkthrough assembled from those
screenshots, with English captions. It is not a continuous live screen recording.

## Reproduce the walkthrough

1. Follow [the build guide](building.md).
2. Launch the Qt executable with `config.example.json`.
3. Capture the initial window.
4. Click **开始面试**. The current Mock automatically supplies three preset answers.
5. Capture the completed conversation.
6. Click **查看报告** and capture the result window.

The published captures use a temporary copy of the default configuration at
`/tmp/ai-interview-demo/config.json`, with only `report.output_directory` changed
to an absolute temporary directory. This keeps personal home-directory paths out
of screenshots. The candidate, questions, answers and scoring rules are unchanged.
Reports remain generated local files and are not committed.

The 95/100 scores in this example are deterministic Mock outputs. To demonstrate
actual answer entry without a service key, use the CLI. A future Qt text-input
mode would make the offline desktop walkthrough interactive.

## Caption timeline

- 0–8 seconds: launch the Qt desktop with a key-free Mock configuration.
- 8–18 seconds: run the three-question interview using scripted answers.
- 18–30 seconds: inspect the report's answers, scores and feedback.

The UI language is currently Chinese; the README and video captions explain the
flow in English for visitors.
