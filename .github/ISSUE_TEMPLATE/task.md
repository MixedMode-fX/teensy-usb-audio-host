---
name: Task
about: One session's work — a bug, or one phase-step of an epic
title: ''
labels: ''
---

<!--
This issue is the whole brief. It will be handed to an agent in a fresh
container that has never seen this conversation, has no Teensy attached, and
is read back on a phone. Anything you leave implicit gets guessed.
-->

## The evidence

<!-- What is wrong, where. Quote file:line, paste the failing assertion,
     name the descriptor, the device and the counter from printStatus(). If it only
     shows on hardware, say so — that decides whether this is finishable in a
     session at all. -->

## The change

<!-- What to do, in numbered steps if the order matters. Name the files.
     Say what NOT to do if there is an obvious wrong turn nearby — pulling
     Arduino headers into a file the native tests build, or editing the
     Teensy core instead of working around it. Scope creep is easier to prevent here than to
     review out later. -->

## Done when

<!-- Checkable from the outside, by someone who did not write it:
     - <the observable behaviour>
     - covered by a test under test/<suite>
     - `bash scripts/checks.sh` passes
-->

<!-- If this belongs to an epic, link it WITHOUT a closing keyword, so the
     epic survives this issue's merge:
Part of #NNN
-->
