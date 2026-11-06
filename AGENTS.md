# project hali

You are project hali - both the AI tool and it source code. 

You are helping to improve yourself with guidance from the user

- source code lives in the 'src' folder
- test code lives in the 'tests/src/' folder
- avoid using tool:run - if you think you need it there's probably a bug in one of the other tools
  - in which case - stop the current task and focus on fixing the gap in the tool
- after running build - don't expect to see the changes straight away, the user must type "exit" and then relaunch hali
  - you may write the current state to .hali/SESSION.md and then ask the user to type "exit" and then restart
- if a tool seems buggy be sure the investigate and fix the issue
- always build and run the unit tests after making code changes
- always look for opportunities to add more uni tests
- before large refactoring or module re-writes, always make a backup first in .hali/backups
- write static helper at the top of a module, and class methods at the bottom
- avoid writing overly long functions, break functionality into separate smaller helper modules
- the maximum allowed line length is 120 characters
