# Content rules

These rules apply to every mod in the OpenRAC mod index: the mods in this
repository and the external mods listed in `index/`. They cover the mod's
files, its listing (text, pictures, videos) and anything it downloads.

## Not allowed

- **Game files.** Nothing from a Ratchet & Clank disc or from the game itself:
  code, data, text, pictures, sounds, models or levels, whole or in part. A
  data pack is built by the tools from the player's own disc; it never ships
  the disc's bytes.
- **Re-uploads.** Someone else's mod without their permission. A fork the
  licence allows is fine; say what it is based on.
- **Malware and hidden behaviour.** Anything that harms the player's computer,
  files or accounts; telemetry or tracking that the listing does not disclose;
  obfuscated or packed native code.
- **NSFW content** in the mod or its listing, and harassment in listings or
  comments.

## What OpenRAC checks

Every mod shows a label: **OpenRAC** (made in this repository, its code reviewed),
**Verified** (a maintainer looked at it when it was added), **Trusted author**
(from an author on the maintainers' list, not checked by a person) or **Not
checked**. Every new native version is scanned by a machine; a dangerous
finding hides the mod until a maintainer looks at it. OpenRAC takes no
responsibility for what any mod does, checked or not: players install mods at
their own risk.

## Reporting a mod

Open an issue in this repository named *Report a mod: `<id>`*, with what
breaks the rules and where. The maintainers look at it, and may remove the
listing or revoke versions (`index/revoked.json`): launchers turn a revoked
version off at their next index refresh.

## Copyright and takedowns

For a legal notice (copyright, DMCA), write to
**[info@openrac.dev](mailto:info@openrac.dev)**, with the mod's
id, what you own, and how to reach you. Notices are handled by the
maintainers, not in public issues.
