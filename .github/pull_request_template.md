# CVS checklist
- [ ] Each commit does only one change at time, and it is expressed clearly in
      the commit message.
- [ ] The commit message includes reference to github & Youtrack issue number

# Code checklist
- [ ] There must be a good reason to use dynamic memory calls (malloc, calloc,
      ...) instead to use stack automatic variable allocation.
- [ ] The code does NOT contain comments that can be omitted, especially
      comments that you can avoid creating another function.
- [ ] Validate all user inputs, fail loudly if not used properly.
- [ ] Check All return codes.
- [ ] Document all modified functions and files:
- [ ] Update changes related documentation & user messages in the same commit
      as changes occur (`README.md` and `docs/` subfolder), and user 
      information messages.
- [ ] New helper functions are justified by the fact that there is no other
      function I can use/extend for my case. If I extend one, I've checked all
      the uses, and I've tried to stick to old API.
- [ ] Update tests cases, especially looking for corner cases and errors path.
      Code coverage should show that tests passed by the code I modified.
- [ ] New libraries/components have a test to show when they are outdated.

# Use of tools
- [ ] Check text changes against these tools:
  - [ ] Grammarly or similar (if available)

# If you modify HTTP listener.

This checks should be enforced by tests system, but currently it's not possible
so you have to check it by hand in every commit.

- [ ] Make sure that you have mlock'ed server private key memory in order to
      avoid operative system to put it in disk swap
- [ ] HTTPS listener checks key permission before use it. Users should not be
      able to read or write it!
- [ ] Memory data is deleted before release it.
