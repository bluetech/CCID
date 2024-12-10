# for ninja:
# meson setup builddir
# cd builddir ; meson compile

# regenerate configuration (run from builddir/)
# meson setup --reconfigure --wipe

project('ccid', 'c',
  meson_version : '>=0.58.0',
  version : '1.6.1')

# for config.h
conf_data = configuration_data({
  'VERSION' : '"' + meson.project_version() + '"',
})

# global arguments
add_global_arguments('-fvisibility=hidden', language : 'c')

# tests for functions
compiler = meson.get_compiler('c')
if compiler.has_function('strlcpy')
  conf_data.set('HAVE_STRLCPY', true)
endif
if compiler.has_function('secure_getenv')
  conf_data.set('HAVE_SECURE_GETENV', true)
endif

# variables
bundle_id = 'ifd-ccid.bundle'
extra_bundle_id = get_option('extra_bundle_id')
if extra_bundle_id != ''
  bundle_id = 'ifd-ccid-' + extra_bundle_id + '.bundle'
else
endif
dyn_lib_ext = '.so'

pcsc_dep = dependency('libpcsclite')
pcsc_cflags = pcsc_dep.partial_dependency(compile_args : true)
libusb_dep = dependency('libusb-1.0')
threads_dep = dependency('threads')

r = run_command('uname', check: true)
pcsc_arch = r.stdout().strip()

# flex generator
gen_flex = generator(find_program('flex'),
  output : '@BASENAME@.c',
  arguments : ['-o', '@OUTPUT@', '--prefix=@BASENAME@', '@INPUT@'])

conf_data.set_quoted('PCSCLITE_HP_DROPDIR', pcsc_dep.get_variable('usbdropdir'))
conf_data.set_quoted('BUNDLE', bundle_id)

# libccid
libccid_src = [
  'src/ccid.c',
  'src/ccid_usb.c',
  'src/commands.c',
  'src/ifdhandler.c',
  'src/simclist.c',
  'src/strlcpy.c',
  'src/utils.c',
  'src/openct/proto-t1.c',
  'src/towitoko/atr.c',
  'src/towitoko/pps.c',
  ]
gen_src = gen_flex.process('src/tokenparser.l')
libccid_src += gen_src

library('ccid',
  libccid_src,
  override_options : ['b_lundef=false'],
  include_directories : ['src'],
  dependencies : [libusb_dep, pcsc_cflags],
  install : true,
  install_dir : join_paths(pcsc_dep.get_variable('usbdropdir'), bundle_id, 'Contents', pcsc_arch))

# Info.plist
command = [
    find_program('src/create_Info_plist.pl'),
    files('readers/supported_readers.txt'),
    '@INPUT@',
    '--version=' + meson.project_version(),
    '--target=libccid' + dyn_lib_ext
    ]
if not get_option('class')
  command += '--no-class'
endif
if extra_bundle_id != ''
  command += '--extra_bundle_id=.' + extra_bundle_id
endif
custom_target('Info.plist',
  output : 'Info.plist',
  input : 'src/Info.plist.src',
  build_by_default : true,
  capture : true,
  command : command,
  install : true,
  install_dir : join_paths(pcsc_dep.get_variable('usbdropdir'), bundle_id, 'Contents'))

# generate config.h
configure_file(output : 'config.h',
  configuration : conf_data)
