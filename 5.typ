// =============================================================
// 11-git-cheatsheet.typ — Acme Git Workflow Cheatsheet
// =============================================================
// A professional single-page Git reference guide.
// Uses the cheatsheet-template to organize commands into
// logical categories with a clean, high-density layout.
//
// What you'll learn:
//   - Organizing data-heavy content into columns
//   - Using custom components for consistent styling
//   - Applying different accent colors for visual grouping
// =============================================================

#import "11-cheatsheet-template.typ": cheatsheet-template, category, cmd, sub

#show: cheatsheet-template.with(
  title: "Git Workflow",
  subtitle: "Acme Widget Platform Standard",
  accent-color: rgb("#2e7d32"), // Green for Git
)

#category("Setup & Config", accent-color: rgb("#2e7d32"))[
  #cmd("git config --global user.name \"[name]\"", "Sets your commit name")
  #cmd("git config --global user.email \"[email]\"", "Sets your commit email")
  #cmd("git init", "Initialize a new local repo")
  #cmd("git clone [url]", "Clone a repository")
]

#category("Basic Snapshotting", accent-color: rgb("#1565c0"))[
  #cmd("git status", "View changed files")
  #cmd("git add [file]", "Stage a specific file")
  #cmd("git add .", "Stage all changes")
  #cmd("git commit -m \"[msg]\"", "Commit staged changes")
  #cmd("git commit --amend", "Edit last commit")
]

#category("Branching", accent-color: rgb("#ef6c00"))[
  #cmd("git branch", "List local branches")
  #cmd("git checkout -b [name]", "Create and switch to branch")
  #cmd("git checkout [name]", "Switch to a branch")
  #cmd("git merge [branch]", "Merge branch into current")
  #cmd("git branch -d [name]", "Delete a branch")
]

#category("Remote Sync", accent-color: rgb("#2e7d32"))[
  #cmd("git fetch", "Get latest from remote")
  #cmd("git pull", "Fetch and merge changes")
  #cmd("git push origin [branch]", "Push branch to remote")
  #cmd("git remote -v", "List remote URLs")
]

#category("Inspection & Log", accent-color: rgb("#6a1b9a"))[
  #cmd("git log", "Show commit history")
  #cmd("git log --oneline", "Compact history")
  #cmd("git diff", "Show unstaged changes")
  #cmd("git diff --staged", "Show staged changes")
  #cmd("git show [commit]", "View specific commit")
]

#category("Undoing Changes", accent-color: rgb("#c62828"))[
  #cmd("git reset [file]", "Unstage a file")
  #cmd("git reset --hard HEAD", "Discard all local changes")
  #cmd("git checkout -- [file]", "Restore file to last commit")
  #cmd("git revert [commit]", "Create a compensating commit")
]

#category("Acme Specifics", accent-color: luma(60))[
  #sub("Branch Naming")
  #cmd("feat/[jira-id]", "For new features")
  #cmd("fix/[jira-id]", "For bug fixes")
  #cmd("docs/[description]", "Documentation only")

  #sub("Commit Format")
  `[JIRA-ID] description`

  #sub("Pre-flight")
  #cmd("npm run lint", "Run before committing")
  #cmd("npm test", "Ensure all tests pass")
]

#category("Stashing", accent-color: rgb("#00838f"))[
  #cmd("git stash", "Temporarily store changes")
  #cmd("git stash pop", "Restore last stashed")
  #cmd("git stash list", "List all stashes")
  #cmd("git stash drop", "Discard last stash")
]

#category("Glossary", accent-color: luma(120))[
  / Working Directory: Current file state.
  / Staging Area: Files ready for commit.
  / Repository: Committed history.
  / Remote: Hosted version (GitHub/GitLab).
  / HEAD: Last commit in current branch.
]