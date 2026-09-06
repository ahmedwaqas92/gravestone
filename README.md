# gravestone
 
Reviving old hardware for AI inference.
 
A desktop application for Windows, Linux and macOS. Reads the machine,
reports which models fit, then runs them in a way that gets more out of
a small model than asking it once.
 
Written in C. Generates any language with a verifier: C, C++, Rust,
Zig, Go, C#, Python, TypeScript, JavaScript, HTML, CSS, SQL.
 
**Contents**
[Overview](#overview) ·
[Sampling](#sampling) ·
[Small models](#small-models) ·
[Hardware detection](#hardware-detection) ·
[The harness](#the-harness) ·
[Verification](#verification) ·
[Prose answers](#prose-answers) ·
[Language adapters](#language-adapters) ·
[Platforms](#platforms) ·
[Installing](#installing) ·
[Interface](#interface)
 
---
 
## Overview
 
Most people are told they need an expensive graphics card to run AI
locally. Usually that is not true. The machine sitting on their desk
can run something useful, but working out which model fits means
knowing how much memory, disk and graphics memory are present, and then
knowing how those numbers translate into model sizes.
 
Gravestone looks at whatever the computer it is running on, works out what
it can handle, and says so. Then it runs models on it in a way that
gets more out of them than asking once and hoping.
 
It runs on Windows, Linux and macOS, from one source. It runs on a
desktop from 2012, a laptop from 2016, and a Raspberry Pi.
 
---
 
---
 
## Sampling
 
A model that fits on an ordinary machine will usually be a small one,
and small models get things wrong most of the time.
 
But they do not get things wrong every time.
 
**For Example.** Take a small model asked for a function that packs an
array of values into bit fields. The answer comes back broken. It omits
one of its two outputs, or indexes the wrong variable, or calls
something that does not exist.
 
**Repeating the Example Multiple Times.** Put the same question to the same model again, with the randomness turned up so each answer differs. Most are broken too, in
different ways. But one of them works. It passes the type checker,
handles the awkward cases where the array length is not a round number,
and produces the right answer for every input tested.
 
**Nothing changed but the seed.** The model did not improve between
attempt one and attempt seven. Nothing was retrained. The working
answer was always somewhere in the range of things that model could
produce, and asking once takes a single draw from that range.
 
**The arithmetic.** At roughly one working answer in ten, a single
attempt fails about ninety times out of a hundred. Ten attempts fail
about thirty-five times out of a hundred. Thirty attempts fail about
four times out of a hundred.
 
Attempts are cheap when they run on hardware that is already paid for.
 
---
 
---
 
## Small models
 
The same behaviour appears across models and across languages.
 
| Behaviour | Consequence |
|---|---|
| Identical answers on textbook problems | Extra models add nothing there |
| Different failures on unusual problems | Extra models add something here |
| Correct fragments spread across answers | Merging them is not possible |
| Empty replies from reasoning models | Truncation is its own failure type |
 
**Textbook problems.** Several models from different companies asked to
reverse the bits in an integer will each produce the same loop working
one bit at a time, and the same convergence shows up on a sorting
function or a REST handler, because every model has seen thousands of
examples of each during training.
 
**Unusual problems.** Given a specific memory layout together with a
rule for handling leftover space, the answers diverge sharply, with one
model omitting an output entirely, another writing mathematical
notation where a loop belongs, and a third calling functions that do
not exist.
 
**Scattered correctness.** One model will place the bits correctly
while getting the surrounding logic wrong, and another will get the
logic right before overwriting its own output at the end, so the two
correct halves exist but never in the same answer. Combining them
requires judgement that the models do not have, which is why merging
fragments is not part of the design.
 
**Empty replies.** A model with a reasoning mode can fill its entire
context deliberating and return nothing at all, which turning reasoning
off will fix, and the harness therefore treats a truncated reply and a
wrong reply as separate failures needing separate handling.
 
> **What gravestone does about it**
>
> Sends every task to every model, since the model that solves an
> unusual case cannot be predicted in advance.
>
> Keeps whole answers only. A candidate passes or is discarded, and no
> attempt is made to assemble one answer out of parts of several.
>
> Sets reasoning off by default and turns it on per model where the
> logs show it earning its cost.
>
> Records four outcomes: passed, failed the static check, failed the
> tests, returned nothing. The last is retried with a larger context.
 
---
 
## Hardware detection
 
Gravestone reads the machine and reports what fits.
 
**Reads.** The processor and its physical core count, total
and available memory, each graphics card and its memory, and every disk
with its free space, including external ones.
 
**Reports.** Each known model sorted into one of four groups:
fits entirely in graphics memory, fits partly with the remainder
falling back to the processor, runs on the processor alone, or will not
run at all.
 
The last line names the disk with the most free space.
 
### The model catalogue
 
Hugging Face.
 
Its model API is public and needs no account for public repositories.
Filtering to the GGUF format narrows the results to models that run
locally. Requesting the full record returns each repository's file
manifest, which carries exact byte sizes for every quantisation the
repository holds.
 
Results are sorted by download count.
 
A snapshot ships inside the program, generated when a release is built.
The list refreshes on request and the result is cached. When the
network is unavailable the snapshot is used, and the report states its
date.
 
The fitting logic reads sizes and nothing else.
 
### Network use
 
Detecting hardware needs nothing. Neither does recommending models,
since the snapshot ships with the program.
 
Refreshing the model list needs a connection. So does downloading a
model, once per model. Everything after that runs locally, including
the harness, which talks to a server on the same machine.
 
Downloading is handled by Ollama or llama.cpp, which resume interrupted
transfers and verify checksums.
 
---
 
## The harness
 
A task is a description of what the code should do plus a set of tests.
 
The harness sends the description to every configured model, several
times each, with randomness high enough that the answers differ. Every
answer goes through the verifier for its language. Those that survive
the static checks are run against the tests. Those that pass are kept.
 
If none pass, the failures go back to the models with the error text
attached and the round repeats, up to a limit. On reaching the limit
the harness reports failure.
 
Nothing in the loop asks a model whether an answer is correct. That
comes from tools that execute it.
 
### Model providers
 
The harness sends HTTP requests to a server already running on the
machine.
 
Ollama, llama.cpp's server, LM Studio, vLLM and most hosted APIs accept
the same request format, originally OpenAI's. One client covers all of
them.
 
A model is three fields: a name, an address, and a sample count. Any
server speaking that format works, including a rented one, though the
point of gravestone is the opposite case. The target is a machine with
little compute, running a small model, made useful by repetition and
verification instead of by better hardware.
 
---
 
## Verification
 
**Never trust a model. Always check the work.**
 
### Self-review
 
Published research on models reviewing each other's code in a loop
found that the reviewer degrades along with the writer. Acceptance
rates climb while correctness falls. The conclusion was that a stable
loop needs verification from outside the models.
 
Models generate. Tools decide.
 
### Language adapters
 
The loop is the same for every language. The tool that decides is not.
 
| Language | Static check | Runtime check |
|---|---|---|
| C, C++ | compiler, warnings as errors | sanitisers, unit tests |
| Rust | `cargo build`, `clippy` | `cargo test`, `miri` |
| Zig | `zig build` | `zig test` |
| Go | `go build`, `go vet` | `go test`, race detector |
| C# | `dotnet build`, analysers | `dotnet test` |
| TypeScript | `tsc --strict`, eslint | test runner |
| JavaScript | eslint | test runner |
| Python | `mypy`, `ruff` | `pytest` |
| React | `tsc`, build | render tests, accessibility checks |
| HTML | validator | rendered structure |
| CSS | validator, stylelint | computed style checks |
| SQL | parser, `EXPLAIN` | run against a fixture database |
 
A language adapter is four functions: write the candidate to disk, run
the static check, run the tests, read the error output.
 
### Runtime checks
 
`1 << n` where `n` reaches 31 shifts a signed integer into its sign
bit, which C leaves undefined. It compiles clean under `-Wall -Wextra`
and passes functional tests. A sanitiser reports it immediately.
 
Python code that satisfies `mypy` still divides by zero at run time.
TypeScript that compiles still carries an `any` hiding a type error.
 
### The verify step
 
1. Runs the static check with warnings as errors.
2. Runs the language's second pass: sanitisers, race detector, stricter
   linter.
3. Executes the result against a test suite written independently.
4. Checks edges: empty input, boundary sizes, values at type limits.
---
 
## Prose answers
 
A task with tests carries its own decider. A question answered in plain
sentences leaves the harness with nothing to run.
 
**Reshaping the question.** Most questions carry a checkable core and
can be asked so a tool decides them. A request to explain how a closure
captures a loop variable becomes a request for a program that
demonstrates it, with a test asserting the captured values. The
compiler and the test run decide as they always do. The explanation
travels with the artefact that survived.
 
**The corpus.** Questions that resist reshaping need documents to check
against. A collection of files is placed on disk, and every quotation
an answer makes must appear in one of them, character for character.
The tool searches for the span and finds it or does not.
 
**Every sentence cites.** An answer carries a source marker on each
sentence. A sentence without one puts the whole answer out of the pool
before any ranking happens, so an answer of five perfect quotations
joined by invented claims never reaches the top.
 
**Overlap.** A sentence naming a real quotation can still misstate it.
The content words of the sentence are compared against the words of the
span it cites. A sentence claiming something its source never mentions
fails that count. Reversing a meaning costs one word, so a negation
carries the vocabulary of the claim it denies and passes.
 
### Choosing among survivors
 
Every answer from every model lands in one pool, and the tool that
checks the quotations also counts, so the count picks the winner.
 
1. Most quotations verified.
2. Fewest sentences failing the overlap check.
3. Shortest answer.
4. Earliest sample, so ties break the same way on every run.
 
The whole answer at the top is shown. No sentence is lifted from a
second answer to patch the first.
 
Model identity stays out of the ranking. Another model is another
source of variety, in the way another seed is. Its answers compete on
the same counts as everything else. Two models producing the same claim
raises no score, since agreement between guessers is agreement between
guessers.
 
### Outcomes
 
| Outcome | Meaning |
|---|---|
| passed | every sentence cites, every quotation checks out |
| failed the citation check | a quoted span appears in no document |
| failed the overlap check | a sentence claims what its own source does not carry |
| returned nothing | truncated or empty, retried with a larger context |
 
> **What gravestone does about it**
>
> Turns a question into a task with tests wherever the question allows
> it, since a compiler decides more than any count of words can.
>
> Checks quotations against documents the operator supplied, character
> for character, and never against the model's memory.
>
> Marks the sentences scoring lowest on overlap and shows them marked,
> so the reader audits four sentences instead of a page.
>
> States plainly that a compiler is an oracle for code and prose has
> nothing of the kind, so the last step belongs to the person reading.
 
None of this is measured. The overlap threshold, the drift it catches,
and the number of sentences left flagged are design sitting ahead of
any experiment.
 
---
 
## Platforms
 
| | Detection | CPU inference | GPU inference |
|---|---|---|---|
| Windows 11 | yes | yes | NVIDIA, AMD, Intel |
| Linux | yes | yes | NVIDIA, AMD, Intel |
| macOS | yes | yes | Apple Silicon |
| Raspberry Pi | yes | small models | no |
 
---
 
## Installing
 
One binary per platform, with llama.cpp bundled alongside it.
 
**Graphics detection.** Vulkan and the vendor GPU tools are opened at
run time. Vulkan is tried first, vendor tools second, and a machine
with neither is reported as processor-only.
 
**Network.** WinHTTP on Windows, CFNetwork on macOS, OpenSSL or the
system curl on Linux.
 
**Inference server.** An existing Ollama install is detected and
preferred. Otherwise the bundled llama.cpp is used.
 
**Models.** A download of several gigabytes is offered and confirmed,
or taken without asking when `--yes` is passed.
 
**From source.** A C compiler and CMake.
 
---
 
## Interface
 
Gravestone opens a window. Everything it does is reachable from there.
 
**Toolkit.** The interface is served over HTTP on localhost and opened
in the browser already installed on the machine. Where Chrome or Edge
is present it opens in app mode, giving a window with no tabs and no
address bar. HTML, CSS and JavaScript on the front, C behind it.
 
**Baseline.** Flexbox, CSS Grid and ES2017. Browsers from 2017 onward.
 
**Scope.** The codebase is public and the GUI is the supported way to
use it. Command-line access exists for continuous integration and is
documented separately.
 
> **GUI first, one interface for everyone.** Semiconductor fabrication
> in a lab and a morning of email are the same task underneath: a
> description goes to a model, an answer comes back, and it has to be
> right. Both get the same window.