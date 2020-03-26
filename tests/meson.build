unit_tests = [
  'named-pipe',
]

winsock2_dep = cc.find_library('ws2_32')

foreach unit: unit_tests
  exe = executable(
    unit, unit + '.c',
    dependencies: [ wing_dep, winsock2_dep ],
    include_directories: wing_inc
  )
  test(unit, exe, args: [ '--tap', '-k' ])
endforeach
