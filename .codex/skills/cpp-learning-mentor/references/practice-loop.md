# Practice Loop

Use this loop for each learning task:

1. Read one small file or function.
2. Explain what it owns, what it receives, and what it returns.
3. Make one tiny change.
4. Build or test immediately.
5. Write down one sentence: "I changed X, so Y behavior changed."

## Exercise Patterns

- Add a field to a config struct and validate it.
- Add one state to an enum and test its display string.
- Replace a hard-coded question with loaded JSON.
- Convert a raw pointer idea into `std::unique_ptr`.
- Add a mock implementation of a service interface.
- Write a test for an event sequence before implementing it.

## Review Questions

- Who owns this object?
- Can this function fail, and how is failure represented?
- Does this code run on the UI thread or worker thread?
- Can this test run without network and secrets?
- What is the smallest command that proves the change works?
