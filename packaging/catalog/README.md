# Reviewed dependency sources

`sources.json` is a trust policy, not a search index. The launcher embeds this
catalog and may load a complete replacement from `<application-data>/sources.json`.
Treat local catalog changes like installing executable software: review the
publisher, official page, repository ownership, tag policy and asset naming policy.
Downloading always requires a separate explicit user action.

Each automatic entry records publisher identity evidence on an official project
or modding documentation page. SMAPI `UpdateKeys` alone never authorize a repository.
Content Patcher's official modding guide links its Pathoschild/StardewMods readme
and Nexus project: <https://stardewvalleywiki.com/Modding:Content_Patcher>.
That documentation was inspected on 2026-10-15. GitHub was not contacted during
implementation, so release availability and the current asset naming convention
remain unverified. The strict policy fails closed to manual download if no unique
matching compatible asset exists.

To maintain the catalog independently of launcher releases, copy the schema-version
1 JSON file to the application data root and review edits locally. A complete
replacement is intentional: removed trust entries must not remain implicitly active.
No automatic catalog update or remote trust expansion is implemented.

Automatic GitHub eligibility requires a reviewed `owner/repository`, HTTPS
`identityEvidence` and `officialPage`, `publisher`, an anchored `assetPattern`, and
`automatic: true`. `tagPrefix` distinguishes projects in a monorepository. Only
non-draft, non-prerelease releases, a comparable version meeting the requested
minimum, and exactly one matching uploaded ZIP asset qualify. Downloads enforce
HTTPS, dedicated GitHub CDN redirects, 30-second inactivity and 60-second request
deadlines, declared/actual size limits, and available API SHA-256 digests. A digest
protects integrity; it is not an independent publisher signature. Cryptographic
attestation verification and checksum-sidecar formats are future work.

Extracted ZIPs must contain exactly one manifest with the approved ID and exact
release version. The existing staging installer enforces archive safety and
rejects every existing ID without replacing configuration. Version upgrades,
disabled-dependency activation, and removals remain explicit local user operations.
Installing a missing dependency never executes its assembly.

Nexus download authentication, entitlement checks and protected downloads are not
implemented. The official Nexus page supports the manual-download path. The
`ModSourceProvider` interface leaves room for a future authorized NXM provider.

Manifest behavior is based on the official SMAPI modding guide:
<https://stardewvalleywiki.com/Modding:Modder_Guide/APIs/Manifest>.
`MinimumApiVersion` and `MinimumGameVersion` constrain SMAPI and Stardew Valley;
`Dependencies[].MinimumVersion` and `ContentPackFor.MinimumVersion` constrain mod
versions. Required dependencies default to true; `ContentPackFor` is always required.
Missing optional dependencies are advisory. Only the selected profile satisfies
requirements. Runtime version checks explicitly remain unknown if versions are
not supplied, since launcher executable discovery does not establish a trustworthy
installed SMAPI/game version.

All version constraints in this manifest schema are minimum constraints. Their
intersection uses the strongest minimum; distinct minimums are not contradictory.
Invalid version syntax is reported rather than inventing a maximum-version policy.
