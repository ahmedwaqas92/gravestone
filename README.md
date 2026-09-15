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
 
**Reports.** Each known model sorted by where it would run. A model the
card holds whole. A model system memory holds whole. A model either
would hold. A model divided between the two. A model too large for the
machine.
 
The last line names the disk with the most free space.
 
### How the list is worked out
 
Every figure below is bytes. `M` is the size of the model file on disk.
 
Weights alone do not run a model, since the working space for one reply
sits beside them. A fifth on top is an allowance rather than a
measurement.
 
    need = M / 5 * 6
 
The machine is asked for its total memory `R`, what it reports free
`F`, the graphics memory fitted `V`, and what the card reports free
`Vf`. A guest inside a virtual machine or a container is noted, since
its memory is an allowance somebody else decided.
 
What the machine already holds is the larger of what it reports and
what a machine in ordinary use carries. The reading is exact for the
moment it was taken and says nothing about the moment after. The
modelled figure holds still and describes a machine somebody is sitting
at. Taking the larger keeps an idle moment from promising room that
vanishes when the person opens their mail.
 
    reserve = max(R - F,  floor + R * 4/100  +  debt)
 
    floor = 1 GB for a desktop of its own, 256 MB otherwise
    debt  = 1372494161 * log2(1 + R / 2 GiB), for a desktop of its own
    the modelled half is capped at 3R/4, so a small machine keeps a quarter
 
A desktop of its own means a screen and no guest. A machine in a rack
takes the smaller floor and no debt, since nobody is sitting at it. A
guest takes the same, because the browser and the mail client are running
outside the allowance it was given and cannot take memory from inside it.
What runs inside the guest shows up in the reading instead.
 
The kernel keeps a record of 64 bytes for every 4096 byte page it owns,
which is 1.56 per cent of memory at every size, rounded up to 4 to cover
its other tables. A browser and a mail client are the same size on every
machine, and people fill the space they are given, so the debt climbs
with memory and flattens. It was calibrated against one laptop holding
3029139456 bytes of user programs in 8241831936 bytes of memory, which is
one reading behind a constant that shapes every verdict.
 
The logarithm is worked out in whole numbers, since a fit verdict has to
come out the same on every machine running the program and floating point
makes no such promise across compilers. Whole doublings are counted by
halving, and the part of a doubling left over is straightened into a
line, which runs under the curve by up to nine per cent of one doubling.
The constant above was fitted through that routine rather than through an
exact logarithm, so the two cannot drift apart. Whole number division
loses a little in both directions, so the constant reproduces the reading
it was fitted to within about a ten thousandth of one per cent rather
than exactly.
 
What is left is what a model may draw on. Graphics memory has few other
tenants, so its free reading is used as it stands.
 
    memory   = R - reserve
    graphics = Vf, or V when the card will not say
 
Models are asked one at a time, so each has the whole machine to itself
and only has to fit on its own. Both stores are asked rather than the
first that answers.
 
    card   = graphics > 0 and need <= graphics
    system = need <= memory
 
    card and system   ->  runs on either
    card only         ->  runs on the card
    system only       ->  runs on the processor
    neither, but need <= memory + graphics  ->  divided
    otherwise         ->  will not run
 
A divided model is loaded card first, since every byte left in system
memory is read across the bus once for every word produced.
 
    on_card   = min(need, graphics)
    in_memory = need - on_card
 
A divided model with too little of itself on the card is left out of the
list somebody picks a download from. It would load and then answer at a
word a minute, which is a download wasted.
 
Where a model runs and whether it is worth fetching are asked apart. A
model already on the disk is never asked the second question, since it
runs however slowly it runs, and telling a person it will not run would
be untrue.
 
    listed only when  need * 70 <= graphics * 100
 
Both sides are multiplied rather than divided, since dividing the left
side first throws away up to nine bytes, which admits a model a hair
under the line.
 
Ollama moves whole layers rather than bytes, so a real run lands on the
layer boundary nearest these figures. The layer count is absent from the
snapshot, so how near is unmeasured.
 
A model has to be kept somewhere before it can be fetched, and one file
sits on one disk, so the largest single disk decides that separately. A
model already on the disk needs no free space to run.
 
Only a model the card holds whole answers at conversational speed,
since every number in the file is read once for every word produced.
 
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

The server holds the weights in memory and answers over HTTP, so it has
to be running before a single question can be asked. It does not survive
the machine restarting, and somebody opening this program on a cold
machine has no reason to know that, so gravestone starts one rather than
telling them to. Starting it happens at startup and returns as soon as
the program has been set going, which takes about 5 seconds to answer on
this machine while the window opens immediately.

Whether one is already running is decided by asking the port rather than
by looking for a program of that name, which is what stops a second one
being started and what makes a server somebody else started count as
theirs. The program is started in a session of its own and handed on to
the system, so it outlives gravestone closing, the way a person expects a
server to behave. What it says goes to `server.log` beside the database.

Nothing is started from inside the harness. A library entry point that
starts a daemon starts one every time anybody checks what it does with
bad arguments, and `make check` runs after every edit.
 
**Models.** A download of several gigabytes is offered and confirmed,
or taken without asking when `--yes` is passed.
 
**From source.** A C compiler and CMake.

**Windows drives under WSL.** A Linux running inside Windows reaches a
Windows drive through a filesystem called drvfs, which is asked for by
hand with `mount -t drvfs D: /mnt/d`. Windows drops those mounts when a
drive is unplugged, when the machine wakes badly, and when the Linux side
restarts, and the row stays in the kernel mount table afterwards while
every read of it fails. Gravestone writes down each drive that worked and
puts it back at the next start.

The rules it keeps to are narrow on purpose, because putting a drive back
means running commands as the administrator.

A drive that answers is never touched, which is the ordinary case and
costs no administrator rights at all. A drive is judged by asking the
filesystem for its size, so a row in the mount table with a dead drive
behind it reads as dead rather than as present. Only a dead drive is taken
down, and only after a walk of every running program shows that nobody is
holding it open, since a model file is mapped into memory rather than read
and pulling the drive out from under the program holding it kills that
program. Nothing is ever removed, so no directory disappears and no
unmount carries the flags that detach a filesystem while writes are still
in flight. A directory already holding files is left alone rather than
mounted over, because mounting hides those files while their bytes go on
filling the disk underneath.

Nothing ever asks for a password. The only form of sudo used is the one
that refuses rather than prompting, and whether it worked is decided by
running `id` and reading back a nought rather than by reading the message,
which is translated. A machine wanting a password gets a warning naming
the command to type by hand, and the program carries on.

The drive letter is the only thing recovered from the database, it has to
be a single capital A to Z, and the mount point is worked out from it
rather than read back. Every command runs with its arguments kept apart
from each other and no shell anywhere, so a row somebody edited can name a
different drive letter and nothing more. `/mnt/c` is never touched, since
the Windows programs behind the file box and the window sweep are reached
through it, and neither is `/mnt/wslg`, which carries the sockets the
screen is drawn through.

A drive plugged in after this Linux started sits in no mount table at
all, so the stored list cannot be the only source. Windows is asked which
drives it has through `fsutil fsinfo drives`, which answers in about a
tenth of a second, and any drive it names that this machine has not
mounted joins the list. That is what makes the very first run put a drive
up rather than find an empty list and stop.

Whether a path is really a mount is decided by its device number rather
than by asking the filesystem for its size. Every mounted filesystem
carries a device number of its own, and an ordinary directory shares the
one belonging to the filesystem above it. An empty `/mnt/d` with nothing
behind it answers a question about free space perfectly well, because the
answer describes the system disk, so a check built on that question alone
reports a drive that is working when there is no drive there.

Putting a drive back needs the administrator, and nobody is ever asked
for a password. Windows grants whoever owns a distribution the right to
start a command inside it as root with nothing asked, since the owner of
the machine already holds every file of it, so under WSL gravestone
reaches root through `wsl.exe -d <distro> -u root -e <program>`. The
program is trusted only from `System32` or `Program Files`, which this
user cannot write to, and the distribution name is refused unless it
holds only letters, digits, dot, dash and underscore. Whether the road is
open is decided by running `id -u` down it and finding a `0` among the
lines that come back, since `wsl.exe` writes warnings of its own on the
error channel and `sudo` is translated. The first call starts a session
and can take ten seconds, and every call after that takes about a tenth
of one. A probe that times out decides nothing and is tried again on the
next pass, since a machine under load is still a machine with the road.
Away from WSL, `sudo` is tried in the one form that refuses rather than
asks. The live probe runs a command as root, so `make check` leaves it
out unless `GS_TEST_ROAD=1` is set.

The drives are watched on a thread of their own, once at startup and
every fifteen seconds after, so a drive plugged in while gravestone is
up comes up on its own and a drive pulled out has its dead entry
cleared. The window never waits on that thread, and reads its disks
again the moment the thread reports a change.

A line in `/etc/fstab` carrying the word `user` does not help under WSL.
`mount` is setuid there as everywhere, and still refuses an ordinary
person, because the drvfs mounting is done by a helper of WSL's own that
answers only to root. A line carrying `noauto` is worse, since WSL stops
mounting that drive at boot. The box that asked for the machine password
and wrote such a line stays in the code, shut wherever a road to root
exists, which under WSL is every machine with `wsl.exe` in its usual
place.

A drive missing for five runs in a row is dropped from the list, so one
unplugged for good stops costing an attempt at every start.

None of this runs anywhere but WSL. Two signals have to agree before
anything happens, the kernel naming Microsoft and a Windows drive being
present in the mount table, because a database carried onto another
machine in a home backup would otherwise aim the unmount command at a real
disk.
 
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
Every answer carries a mark that opens the record behind it. A model
that writes out its reasoning before answering hands that reasoning back
in a field of its own, and the record shows it first, word for word,
under the name of the model that wrote it. How the run went follows,
which models were asked, how long each took, what the checking made of
each answer. The reasoning is kept in the database beside the answer, so
the record opens after a restart as well. It is written in plain ASCII
for the screen, since the glyph code draws nothing else, while the
database keeps what the model wrote.

The panel shows one history drawn from every conversation in the file,
in the order the prompts were asked. It used to show a single
conversation, and a run from the command line or a fresh conversation
started by the window each split the history, so prompts a person had
sent sat somewhere the panel was not looking. New prompts still go into
the conversation the window wrote down as its own.

Every prompt gives two lines. The second is the answer that was shown,
or a line saying no answer came back, or that every answer failed the
checking, and every one of those second lines carries the mark that opens
its record. The record says when the prompt was asked and answered, what
the model worked out, and how the run went, and for a prompt with nothing
shown it says why. A run writes its own record when it finishes, so a run
started from the command line keeps one as well.

The history scrolls with the wheel and with a bar that can be dragged,
and the newest line stays against the box until the reader scrolls back.
One page holds the newest 63 prompts. A line at the top of a page loads
the one before it, which opens at its newest line so reading carries
straight on upwards, and a line at the bottom goes back.

Each bubble is measured once, by wrapping its text to the width of the
panel, and the measurement is kept until the lines or the window change.
Before that, placing one bubble measured every bubble above it, and every
line was measured again from its start for each letter added, so a page
of 64 answers of about 4,600 characters took 28 seconds to paint. It
takes 11 milliseconds now, with a hundred movements of the pointer
counted in.
