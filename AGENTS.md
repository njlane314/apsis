# Agent Instructions

Before writing, reviewing, or modifying code in this repository, read:

- [docs/nasa_coding_guardrails.yml](docs/nasa_coding_guardrails.yml)
- [CODING_STANDARD.md](CODING_STANDARD.md)

Apply those guardrails as repository policy for agent work. This project is not
claiming NASA compliance; the guardrails are an agent-readable distillation for
safer C and Unix-tool development.

Default profile for this repository is `non_nasa_general_software` unless the
user or a project document explicitly marks work as mission-critical,
safety-critical, security-sensitive, embedded, real-time, flight, or ground
support software.

When changing C code:

- Prefer simple, statically analyzable C99.
- Avoid dynamic allocation in runtime paths unless justified.
- Keep loops bounded by inspection where practical.
- Check return values and validate boundary inputs.
- Do not suppress compiler or static-analysis findings without a written
  rationale.
- Record approved deviations in `docs/WAIVERS.md` and unresolved findings in
  `docs/NONCONFORMANCES.md`.
- Run `make check CFLAGS='-std=c99 -Wall -Wextra -Wpedantic -Werror -O2'`
  before delivery when the toolchain supports it.

Never state that this repository, code, or generated output is NASA compliant,
flight software ready, safety certified, or secure by NASA standards unless the
required standard selection, tailoring, waivers, evidence, and approval records
exist in the repository.
