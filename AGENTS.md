# CLI spike

When resuming work on `spike/mod-cli`, read [docs/cli/HANDOFF.md](docs/cli/HANDOFF.md)
first. It records the reviewed snapshots, remaining findings, and verification status.
Consult [CONTRIBUTING.md](CONTRIBUTING.md) for repository conventions and upstream policy.

Build and test through `./containers/spike-build.sh build` and
`./containers/spike-build.sh test`; the host lacks the required Java compiler.
Keep commits local unless the user explicitly authorizes a push or upstream submission.
