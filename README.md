# bronx

Server management bot built on stoat++. Features MariaDB configurations, custom prefixes, welcome/leave actions, reaction roles, cleanups, reminders, command override rules, anti-raid protection, and a highly customizable event logging engine.

---

## Configuration

Place a `.env` file in the working directory:

```ini
TOKEN=YOUR_BOT_TOKEN
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=bronx
DB_PASSWORD=secret
DB_NAME=bronx
```

* **Command execution notes**: The default prefix is `.`. Pre-server custom prefixes are supported. Command names are case-insensitive. Editing a command message re-runs the updated parameters.

---

## Required Permissions by Default

The bot checks for appropriate user/bot permissions. The default permissions required to invoke commands (unless modified via the `.command` config system) are:

* **Administration & Configuration**:
  * `trust` / `command` — Requires being the Server Owner, Bot Owner, or having the `Manage Server` permission (or being registered on the server trust list).
  * `prefix add` / `prefix remove` — Requires `Manage Server` permission (or server trust list / owner).
* **Moderation**:
  * `ban` — Requires the executor and bot to have `Ban Members` permission, plus a valid hierarchy check.
  * `kick` — Requires `Kick Members` permission plus hierarchy check.
  * `timeout` — Requires `Timeout Members` permission plus hierarchy check.
  * `cleanup` / `pin` / `unpin` — Requires `Manage Messages` permission.
* **Welcome / Leave & Logs Setup**:
  * `welcome` / `leave` / `log` — Requires `Manage Server` permission.
* **Anti-Raid**:
  * `antiraid` / `ar` — Requires `Manage Server` permission (or being the Server Owner / trusted user).
* **Reaction Roles**:
  * `rr` — Requires `Manage Roles` permission.
* **Roles**:
  * `role` / `roleinfo` — Requires `Manage Roles` permission (changed from Assign Roles).
* **General / Utility**:
  * `ping` / `uptime` / `say` / `remind` — Available to all users by default.

---

## Command Reference & Examples

### 1. General & Utility

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `help` | `.help` or `.h` | `.help` | Launches an interactive tabbed help menu with pagination and category navigation. |
| `ping` | `.ping` | `.ping` | Checks bot response latency and API heartbeat. |
| `uptime` | `.uptime` | `.uptime` | Displays relative launch time and absolute runtime. |
| `say` | `.say <text>` | `.say Hello world!` | Repeats text back to the channel. |
| `remind` | `.remind <duration> <msg>` | `.remind 2h30m check oven` | Sets a reminder. Durations support `s`, `m`, `h`, `d`, `w`, `mo`, `y`. |
| `reminders` | `.reminders` | `.reminders` | Lists all your active reminders. |

> **Deleted Channel Reminder Fallback**: If a reminder becomes due but the target channel was deleted, Bronx automatically creates a direct message channel and DMs the reminder to the user.

---

### 2. Moderation

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `ban` | `.ban <@user\|ID> [reason]` | `.ban @Syvor Spamming` | Bans a member from the server with an optional reason. |
| `kick` | `.kick <@user\|ID>` | `.kick 01KW4BF...` | Kicks a member from the server. |
| `timeout` | `.timeout <@user\|ID> <duration>` | `.timeout @user 1h` | Times out a member (e.g. `30m`, `2h`, `1d`). |
| `cleanup` | `.cleanup [amount]` (alias `.cu`) | `.cleanup 50` | Deletes bot responses and command invocations in the last 100 messages. |
| `pin` | `.pin [message_id]` | `.pin` (as reply) | Pins the replied message or the target message ID. |
| `unpin` | `.unpin [message_id]` | `.unpin 01KW...` | Unpins the replied message or the target message ID. |
| `modlogs` | `.modlogs <@user\|ID> [actions]` | `.modlogs @user` | View logs taken against a user, or actions they performed. |

---

### 3. Prefix Configuration

Configure custom prefix characters for a server or channel.

* **`prefix list`**
  * Displays active prefixes.
* **`prefix add <guild|channel> <prefix>`**
  * `.prefix add guild !` -> Adds `!` prefix server-wide.
  * `.prefix add channel $` -> Adds `$` prefix in the current channel only.
* **`prefix remove <guild|channel> <prefix>`**
  * `.prefix remove guild !` -> Removes prefix from server-wide list.

---

### 4. Custom Command Rules (`.command`)

The command config module allows you to allow/restrict commands or module groups to specific channels, roles, or users with specific permissions.

* **`trust <@user|@role|ID>`**
  * Toggle trusted status for a user/role. Trusted entities can run administrative commands without requiring explicit permissions.
  * *Example*: `.trust @Moderator` / `.trust 01KW...`
* **`command view <cmd/module>`**
  * View current restrictions configured for a command or module.
  * *Example*: `.command view ban`
* **`command allow <cmd/module> <#channel|@role|permission>`**
  * Whitelists a channel, role, or permission to run a command/module.
  * *Example*: `.command allow moderation #mod-spam`
  * *Example*: `.command allow say @Staff`
* **`command restrict <cmd/module> <#channel|@role|permission>`**
  * Blacklists a channel, role, or permission from running a command/module.
  * *Example*: `.command restrict ping #general`
* **`command disable <cmd/module>`**
  * Disable a command or module completely on the server.
  * *Example*: `.command disable say`
* **`command enable <cmd/module>`**
  * Re-enable a disabled command or module.
  * *Example*: `.command enable say`

---

### 5. Reaction Roles (`.rr`)

All subcommands require the **Manage Roles** permission. Users cannot add roles above their highest position, and dangerous roles (e.g., Manage Roles, Ban, Kick) require typing `yes` to confirm.

* **`rr list`**
  * Lists configured modals in the server.
* **`rr create <name>`**
  * `.rr create verified` -> Creates a blank reaction roles modal in the current channel.
* **`rr send <name> [#channel]`**
  * `.rr send verified #get-roles` -> Sends the modal and sets up all emoji reactions.
* **`rr add <target> <emoji> <role>`**
  * `.rr add verified 🌟 @Star` -> Adds the mapping to the `verified` modal.
* **`rr edit <name> <emoji> <emoji|role> <new_val>`**
  * `.rr edit verified 🌟 role @SuperStar` -> Edits role assigned by the emoji.
* **`rr parse <name> <msg_id|^>`**
  * `.rr parse colors ^` -> Scans the message above for emoji-role rules and imports them into a new modal.
* **`rr parse <msg_id|^>`**
  * `.rr parse 01KW...` -> Scans the message and activates the rules directly on it with a confirmation prompt.
* **`rr delete <name|msg_id>`**
  * `.rr delete verified` -> Deletes a modal template and removes reactions from all its instances.

---

### 6. Welcome & Leave Customization

Allows greeting and departure messages, along with automatic joining roles. Both text and rich JSON formats are supported.

* **`.welcome setup` / `.leave setup`**
  * Start the interactive wizard to set channel, layout format, embed titles, color, and message body.
* **`.welcome role <@role>`**
  * Set a role assigned to users automatically on arrival (autorole).
* **`.welcome channel <#channel|none>`**
  * Re-routes greetings or disables them.
* **`.welcome type <text|embed>`**
  * Toggles layout format.
* **`.welcome message <text...>`**
  * Sets raw greeting text. Supports formatting.
* **`.welcome json <raw_json>`**
  * Import a raw JSON description of the embed (see the JSON syntax guide below).
* **`.welcome edit <key:val...>`**
  * Inline edits fields. E.g. `.welcome edit title:Hello! color:#E74C3C`
* **`.welcome test`**
  * Simulates a user arrival to test the configuration.

#### Variables & Placeholders:
* `{user}`: Mentions the arriving/departing member (`<@ULID>`).
* `{username}`: Raw username of the member.
* `{server}`: Name of the server.
* `{membercount}`: Total members in the server.

---

### 7. Event Logging & Message Customization

Audit trail tracking for edits, deletes, joins, leaves, mod actions, and global user profile changes.

* **`.log aio`**
  * Auto-setup: Creates a `"Logging"` category and channels `#edits`, `#deletions`, `#joins-leaves`, and `#moderation`.
* **`.log channel <event> <#channel|none>`**
  * Binds an event category to a log channel. E.g. `.log channel delete #deletions`
* **`.log message <event> <text...>`**
  * Modifies log message body content.
* **`.log edit <event> <key:val...>`**
  * Updates embed fields inline (e.g. `.log edit delete title:Message Gone color:#FF0000`).
* **`.log json <event> <raw_json>`**
  * Directly import custom JSON layouts for events.

#### Event Keys & Variables:

| Event Type | Key | Placeholders | Default Behavior |
| :--- | :--- | :--- | :--- |
| Message Deletions | `delete` | `{author}`, `{username}`, `{channel}`, `{content}` | Shows deleted text, author, and channel details. |
| Message Edits | `edit` | `{author}`, `{username}`, `{channel}`, `{before}`, `{after}` | Lists before and after states. |
| Member Joins | `join` | `{user}`, `{username}`, `{membercount}` | Audits arrival and updates total member count. |
| Member Leaves | `leave` | `{user}`, `{username}`, `{membercount}` | Audits departure and updates total member count. |
| Moderation Actions | `mod` | `{action}`, `{moderator}`, `{target}`, `{reason}`, `{details}` | Logs bans, kicks, and timeouts with reasons. |
| Profile Changes | `user` | `{user}`, `{oldUser}`, `{newUser}` | Triggers globally on username edits for server members. |

---

### 8. Anti-Raid Protection (`.antiraid` / `.ar`)

Automated protection against join floods, message spam, and suspicious account profiles. Settings are server-scoped and persist in the database. Only the **server owner**, **bot owner**, or **trusted users / Manage Server** members may configure these rules.

#### Subcommands

* **`.antiraid`** / **`.antiraid status`**
  * Displays the current configuration for all anti-raid rules.
* **`.antiraid setup`**
  * Launches an interactive 5-step wizard to configure all rules at once.
* **`.antiraid join <count> <seconds> [action]`**
  * Configure join flood detection. Triggers when `<count>` or more users join within `<seconds>` seconds.
  * *Example*: `.antiraid join 5 60 kick` — kicks the triggering user when 5+ users join in 60 s.
* **`.antiraid spam <count> <seconds> [action]`**
  * Configure message spam detection. Triggers when a single user sends `<count>` messages within `<seconds>` seconds.
  * *Example*: `.antiraid spam 5 2 timeout` — times out users who send 5 messages in 2 s.
* **`.antiraid profile <action>`**
  * Set the action taken when a user joins with a default (no-avatar) profile.
  * *Example*: `.antiraid profile ban`
* **`.antiraid reset`**
  * Resets all thresholds and actions back to defaults (all actions: `none`).

#### Actions

| Action | Behaviour |
| :--- | :--- |
| `ban` | Permanently bans the user from the server. Reason includes the rule name. |
| `kick` | Removes the user from the server. |
| `timeout` | Applies a 10-minute server timeout (mute). |
| `warn` | Sends a DM warning to the user explaining which rule was triggered. |
| `none` | Logs the event (if a logging channel is configured) but takes no further action. |

#### Anti-Raid Event Logging

All triggered anti-raid events are dispatched through the standard logging system under the event key **`antiraid`**. During `.log aio` setup, antiraid events are automatically routed to the **#moderation** channel.

To manually route antiraid logs:
```
.log channel antiraid #your-log-channel
```

To customise the embed appearance:
```
.log edit antiraid title:🛡️ Raid Detected color:#D63031
```

#### Default Thresholds

| Rule | Default Threshold | Default Action |
| :--- | :--- | :--- |
| Join Flood | 5 joins / 60 s | `none` |
| Spam | 5 messages / 2 s | `none` |
| Default Profile | On any join with no avatar | `none` |

> **Note**: All rules default to `none` until explicitly configured — antiraid is passive until you set an action.

---

### 9. Custom Message Overrides (`.message`)

Configure the customized DM or public channel notification dispatched when a moderation action (ban, kick, timeout, warn) is executed.

* **`message list`**
  * Lists configured action notification templates.
* **`message show <action>`**
  * Displays template structure and format. E.g., `.message show ban`
* **`message type <action> <text|embed>`**
  * Toggles the message layout between standard text and embeds.
* **`message channel <action> <dm|public|none>`**
  * Configures target delivery. `dm` messages the user, `public` posts in the moderation channel, `none` disables notification.
* **`message set <action> <text...>`**
  * Overrides the template body. E.g., `.message set ban You were banned from {server} for: {reason}`
* **`message edit <action> <key:val...>`**
  * Inline edits fields. E.g. `.message edit ban title:Server Ban color:#D63031`
* **`message json <action> <raw_json>`**
  * Imports custom JSON templates (see syntax guide below).

#### Notification Variables:
* `{user}`: Mentions the target member.
* `{username}`: Raw username of the target.
* `{moderator}`: Mention of the executor.
* `{server}`: Name of the server.
* `{reason}`: Reason provided for the action.
* `{details}`: Action details (e.g. timeout duration).

---

## Embed JSON Import Guide

Commands accepting `<raw_json>` (`.welcome json`, `.leave json`, `.log json <event>`, and `.message json <action>`) parse standard JSON structures to build custom rich embeds. 

#### Valid Keys:
* `"title"`: String
* `"description"`: String (supports newlines and placeholders)
* `"url"`: String (navigational link)
* `"colour"` / `"color"`: Hex color string (e.g. `"#E74C3C"` or `"random"`)
* `"icon_url"`: String (avatar thumbnail url)
* `"image"`: String (large image attachment url)

#### JSON Example:
```json
{
  "title": "Welcome to {server}!",
  "description": "Glad you made it, {user}! You are our {membercount}th member.\nBe sure to check out <#01KW...>",
  "colour": "#3498DB",
  "image": "https://example.com/banner.png"
}
```
If the JSON is invalid, the bot will return a parsing error with line and character offsets.