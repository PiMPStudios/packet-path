# Campaign Mode — Feature Contract

Status: Implemented
Campaign: **Build Your Own ISP**

## Player experience

Campaign mode turns the existing scenario catalog into a persistent learning journey. The player starts with one unlocked mission, completes it for a one-to-three-star rating, and unlocks the next mission. The Continue button always opens the first incomplete mission. Completed missions remain available for replay, and a better result replaces the previous best-star rating.

The campaign wraps the technical curriculum in five operating phases:

| Chapter | Missions | Narrative role |
| --- | --- | --- |
| First Customers | 1–4 | Bring up the first LANs, VLANs, and customer gateways |
| Regional Backbone | 5–9 | Expand into OSPF and BGP routing |
| Managed Services | 10–12 | Add security, NAT, and tenant overlays |
| Provider Core | 13–19 | Build MPLS, recovery, RSVP-TE, SR-MPLS, and SRv6 capabilities |
| Network Operations | 20–21 | Operate application-aware WAN paths and resolve a compound outage |

Campaign is one path through the simulator, not a restriction on it. Freeplay exposes every discovered level and Sandbox remains available for unconstrained topology building. Freeplay completions never alter campaign progress.

## Progression rules

- The first mission is unlocked for a new profile.
- Completing a mission unlocks the next mission in campaign order.
- Ratings are stored as the best result per mission; replay cannot reduce a rating.
- Continue selects the first mission without a recorded rating.
- The final mission completes the campaign and disables Continue/Next.
- Reset Progress uses a two-click confirmation and clears only campaign ratings.

## Data contract

Campaign structure lives in `levels/campaign.json`. Chapters contain stable IDs, display text, and ordered level numbers. The loader validates the manifest against the discovered level catalog, rejects duplicate chapter IDs or missions, and permits future freeplay levels to exist before they are assigned to a campaign chapter.

Progress is a versioned JSON object:

```json
{
  "version": 1,
  "bestStars": {
    "1": 3,
    "2": 2
  }
}
```

The game treats progress files as untrusted input: version, key, type, range, and entry-count validation happen before loaded data replaces the in-memory profile.

## Persistence

Progress is stored per operating-system user rather than beside the installed executable:

- macOS: `~/Library/Application Support/PacketPath/campaign-progress.json`
- Windows: `%APPDATA%/PacketPath/campaign-progress.json`
- Linux: `$XDG_DATA_HOME/packet-path/campaign-progress.json`, falling back to `~/.local/share/packet-path/`

`PACKET_PATH_PROGRESS_FILE` overrides the path for testing and portable deployments. Saves use a temporary file before replacement so an interrupted write is less likely to leave partial JSON.

## Explicitly deferred

- Multiple named profiles
- Steam Cloud synchronization and achievements
- Currency, equipment, or protocol-skill trees
- Branching mission choices
- Cutscenes, voiced characters, or a separate narrative scene system
- Campaign-specific topology variants that duplicate existing level content

These are deferred until the linear progression layer has been playtested across the full curriculum.
