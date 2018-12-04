unit_tests = [
  'named-pipe',
  'poll',
]

foreach unit: unit_tests
  exe = executable(unit, unit + '.c',
                   dependencies: wing_dep,
                   include_directories: wing_inc)
  test(unit, exe, args: [ '--tap', '-k' ])
endforeach
