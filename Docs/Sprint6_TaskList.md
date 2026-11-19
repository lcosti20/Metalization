# Sprint 6: Final Polish, Trailer + Presentation

**Project:** Metalization, a third-person robot shooter where you hack enemy robots and fight as them
**Sprint start:** 2026-11-18 (backlog written on day 1)
**Starting point:** `main` at the Sprint 5 merge (Production Beta, Test Candidate 02)

**Sprint goal:** finish Operation Redline as a polished, portfolio-ready mission, and present it. That means:
- acting on the TC02 playtest and running one more playtest round,
- a final polish pass and Test Candidate 03 (the final build),
- a development trailer with title, author, project description and sound, posted to YouTube and the Course Outcome Portal,
- a rehearsed 7-minute final presentation,
- submitting the source project, the assignment report and the final files.

The goal is met when every backlog item below is checked off.

---

## A6 deliverables

| Track | Deliverable | Status |
|---|---|---|
| Game | Final Polish | ⬜ Backlog: [Final polish](#final-polish) |
| Game | Source Projects Submission | ⬜ Backlog: [Submission](#submission) |
| Game | Presentation | ⬜ Backlog: [Final presentation](#final-presentation-7-minutes) |
| All | Development Trailer (YouTube + Course Outcome Portal) | ⬜ Backlog: [Development trailer](#development-trailer) |
| All | Assignment report `FA26_lcosti20_A6.pdf` on Blackboard | ⬜ Backlog: [Submission](#submission) |
| Environments / Character | Showreel, postmortem, final scene setup | ➖ Art tracks waived by the professor (solo programmer, graybox art). The trailer is the showreel, and the presentation's "Plan for future" covers the postmortem. |

---

## Production schedule

Time is blocked out each day for production work. Sprint reviews happen in class on the review days listed in the syllabus.

| Block | Focus |
|---|---|
| Days 1-2 | Backlog (this page), playtest follow-ups, start the final polish |
| Days 3-4 | Final polish, playtest round 3, TC03 build |
| Day 5 | Capture trailer footage from TC03, edit the trailer |
| Day 6 | Post the trailer; build the presentation deck |
| Day 7 | Rehearse the presentation (timed), assignment report, Dropbox submission |
| Due day | Present (shirt and shoes) |

Work is backed up to GitHub (`lcosti20/Metalization`, one PR per sprint branch) and copied to Dropbox at the end.

---

## Sprint backlog

### Playtest follow-ups (from the TC02 report)
- [ ] **Respawn clarity (issue 1):** make it clear which robots are rebuilt by a dispenser. Show the dispenser link (a beam or marker on dispenser robots), and have the Guide mention it on the first dispenser.
- [ ] **Sniper lock-on (issue 2):** check the Marksman's aggro trigger. Either delay the laser sight until the player can be shot, or keep it as foreshadowing and say so in dialogue.
- [ ] **First-hack tutorial (issue 3):** a one-time prompt the first time the hack puzzle opens (swap the wires, then Save & Compile).
- [ ] Smoke scenarios for each fix (re-run `dispenser`, `sniper`, `hack`).

### Final polish
- [ ] Full hand playthrough of Operation Redline, start to finish; log every bug and rough edge here.
- [ ] Balance pass: damage, suspicion times, barrage timing, lift speed, boss rockets.
- [ ] Camera shake on explosions, heavy hits and the boss's rockets.
- [ ] HUD and menu pass: text overlap, alignment, gamepad navigation on every menu.
- [ ] Audio mix: music vs effects vs voices levels, no clipping on explosions.
- [ ] Results screen: check the numbers (time, kills, deaths, accuracy) after a checkpoint reload.
- [ ] Title screen: make it trailer-ready (camera framing, logo text, music).
- [ ] Remove debug-only output from the shipping path (on-screen debug text, test maps from the Level Select).
- [ ] Regression run of all smoke scenarios (`alpha`, `bridge`, `lift`, `swap`, `bunker`, `sneak`, `sniper`, `hack`, `hackfail`, `checkpoint`, `lowhealth`, `pickups`, `field`, `dispenser`, `liftai`, `drop`, `hop`, `ditch`, `fx`, `combat`, `gate`, `boss`, `title`).

### Test Candidate 03 / playtest round 3
- [ ] Package TC03 (Windows, Shipping or Development) with every map in the cook list.
- [ ] Playtest with at least two players (classmates plus an outside player), using the same report format as Sprint 5.
- [ ] Fix anything rated High from the playtest; record the rest under "Plan for future".
- [ ] Capture playtest footage (with permission) for the presentation.

### Development trailer
Target length: 60-90 seconds, 1080p60, captured from TC03.

- [ ] **Title card:** "Metalization", author name, course (SCAD GAME 405, Fall 2026).
- [ ] **Project description card:** one line, e.g. "Hack enemy robots and fight as them."
- [ ] **Shot list** (each 3-6 s):
  - [ ] Valley flyover of Operation Redline at the late-afternoon lighting.
  - [ ] Laser and Repeater firefight with tracers and impacts.
  - [ ] Hack Tether locking on, the hack puzzle (wire swap, Save & Compile), taking control of the robot.
  - [ ] Console disguise: a crouched robot folding into a terminal while enemies walk past.
  - [ ] Sprinting through the artillery barrage in the trench.
  - [ ] Riding a gravlift; an enemy following on the lift.
  - [ ] Sniper perch: scope view and a long shot.
  - [ ] Weapon-arm swap animation.
  - [ ] Robot dispenser assembling a robot, then being shut down from a console.
  - [ ] Boss fight: Brute rockets, explosion, mission complete.
  - [ ] Space battle flyby (shows the wider scope of the project).
- [ ] **Sound:** the game's background music track (Pixabay, credited) mixed under the in-game effects; trailer ends on an explosion or the mission-complete sting.
- [ ] **End card:** title, author, "Made in Unreal Engine 5", music credit.
- [ ] Edit (cuts on action, matching the music's beat), export, watch it full-screen once for errors.
- [ ] Upload to YouTube (unlisted or public) with title, description and credits.
- [ ] Post the link on the Sprint 6 section of the Course Outcome Portal.

### Final presentation (7 minutes)
- [ ] Build the deck following the required content:

| # | Section | Time | Content |
|---|---|---|---|
| 1 | Concept / problem to solve | 0:45 | Third-person shooter where hacking is the main verb: you fight *as* the enemy. Problem: making possession readable and fair. |
| 2 | Final outcome | 1:45 | Trailer (60-90 s), plus final screenshots (VFX, lighting pass 3, hack puzzle). |
| 3 | Research / mood boards | 0:30 | Robot-shooter references, industrial sci-fi palette, Blueprint graphs as the hack-puzzle look. |
| 4 | Gameplay diagrams / level layout | 0:45 | Operation Redline's 13 beats from camp to extraction (`Docs/ProductionAlpha.md`), map layout with checkpoints. |
| 5 | Evolution of design, step by step | 1:30 | Milestones table below, plus how the hack went too easy → too hard → readable. |
| 6 | Playtest footage | 0:45 | Clips from the TC02 and TC03 playtests and what changed because of them. |
| 7 | Plan for future | 0:30 | See below. |
| | Buffer | 0:30 | Q&A or overrun. |

- [ ] Rehearse at least once with a timer; cut until it fits in 7:00.
- [ ] Test the trailer plays in the room (local copy as well as YouTube).
- [ ] Presentation day: shirt and shoes.

### Submission
- [ ] Source project to Dropbox > Final Submission (project folder without `Binaries`, `Intermediate`, `Saved`, `DerivedDataCache`), plus the TC03 build.
- [ ] Final merge of the Sprint 6 branch into `main` on GitHub.
- [ ] Screenshot the Course Outcome Portal pages (Awesome Screenshot), compile them into `FA26_lcosti20_A6.pdf`.
- [ ] Submit the assignment report to Blackboard.
- [ ] Mark this page's checkboxes and add a "Sprint 6 results" section.

---

## Project evolution: milestones

For section 5 of the presentation.

| Date | Milestone | What it added |
|---|---|---|
| 2026-09-23 | Project start | Asset pipeline (Blender graybox → UE import → maps), core robot shooter, the Outpost demo |
| 2026-10-19 | Space battle | Flight, capital ships with streamed interiors, hangar portals, battle mode with AI pilots |
| 2026-11-01 | Dialogue system | Radio dialogue and mission objectives |
| 2026-11-02 | Sprint 4: Production Alpha | Operation Redline mission, sprint, sneak, sniper, gravlifts, arm swap, 47 synthesized sounds, TC01 |
| 2026-11-17 | Sprint 5: Production Beta | Hack puzzle and firewall, checkpoints, robot dispensers, AI traversal, Niagara VFX, lighting pass 3, TC02 playtest |
| Sprint 6 | Final | Playtest fixes, final polish, TC03, trailer, presentation |

---

## Plan for future

Draft for the last presentation slide; finalize after the TC03 playtest.

- **Authored art:** replace the graybox models with modelled, rigged robots. The pipeline is model-agnostic, so new characters only need an FBX, an archetype asset and a rig profile.
- **More hacking verbs:** turret possession, robots that open specific doors, possession-only routes.
- **More missions:** a campaign that links ground missions with the space battle, with saved progress and weapon upgrades.
- **AI depth:** cover selection, commanders that buff their squads.
- **Release:** a public demo build, and a YouTube playthrough by the Sprint 5 playtester.
