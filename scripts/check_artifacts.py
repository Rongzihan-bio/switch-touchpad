#!/usr/bin/env python3
"""Structural checks; these deliberately make no claims about on-device behavior."""
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/switch'
def u32(data, offset): return struct.unpack_from('<I', data, offset)[0]
def u64(data, offset): return struct.unpack_from('<Q', data, offset)[0]

def check_memory_configuration(npdm, expected_pool, expected_type):
    aci, acid = u32(npdm, 0x70), u32(npdm, 0x78)
    # ACID +0x208 is the version byte, not the flags. Loader reads flags +0x20C.
    pool = (u32(npdm, acid+0x20C) >> 2) & 0xF
    assert pool == expected_pool, f'Wrong physical pool: {pool}, expected {expected_pool}'
    for label, base, offset in (('ACI0',aci,0x30),('ACID',acid,0x230)):
        start, size = base+u32(npdm,base+offset), u32(npdm,base+offset+4)
        assert size % 4 == 0 and start+size <= len(npdm)
        types = [(u32(npdm,pos) >> 14) & 7 for pos in range(start,start+size,4)
                 if u32(npdm,pos) & 0x3FFF == 0x1FFF]
        # An omitted application_type is classified as System by PM.
        assert types == [expected_type] or (expected_type == 0 and not types), f'{label}: wrong PM application type {types}'

def nso_image_size(nso):
    # Match Loader's page-aligned mapping calculation, including segment gaps.
    last = max(u32(nso,o)+u32(nso,o+4) for o in (0x14,0x24,0x34))
    return (last+u32(nso,0x3C)+0xFFF) & ~0xFFF

def check():
    for name in ('touchpad', 'touchpad-overlay', 'touchpad-test', 'touchpad-diag', 'touchpad-probe'):
        elf = (OUT / (name + '.elf')).read_bytes()
        assert elf[:6] == b'\x7fELF\x02\x01', name
        assert struct.unpack_from('<H', elf, 18)[0] == 183, 'ELF must target AArch64'
    config = json.loads((ROOT / 'sysmodule/touchpad.json').read_text())
    npdm = (OUT / 'touchpad.npdm').read_bytes()
    assert npdm[:4] == b'META'
    aci, acid = u32(npdm, 0x70), u32(npdm, 0x78)
    assert npdm[aci:aci+4] == b'ACI0' and npdm[acid+0x200:acid+0x204] == b'ACID'
    program = int(config['program_id'], 16)
    assert u64(npdm, aci+0x10) == program
    assert u64(npdm, acid+0x210) == program and u64(npdm, acid+0x218) == program
    assert npdm[0xE] == config['main_thread_priority'] and npdm[0xF] == 3
    assert u32(npdm, 0x1C) == int(config['main_thread_stack_size'], 16)
    assert config['pool_partition'] == 1
    check_memory_configuration(npdm, expected_pool=1, expected_type=2)
    # libnx threadCreate/threadClose map and unmap the stack. Check the actual
    # encoded capabilities, not only the JSON that npdmtool was given.
    kac = aci + u32(npdm, aci+0x30)
    kac_size = u32(npdm, aci+0x34)
    assert kac_size % 4 == 0 and kac+kac_size <= len(npdm)
    allowed_svcs = set()
    for offset in range(kac, kac+kac_size, 4):
        cap = u32(npdm, offset)
        if cap & 0x1F == 0xF:
            group = cap >> 29
            mask = (cap >> 5) & 0xFFFFFF
            allowed_svcs.update(group*24+bit for bit in range(24) if mask & (1 << bit))
    assert {0x04, 0x05, 0x08, 0x09, 0x0A} <= allowed_svcs, 'Missing libnx thread lifecycle SVC permissions'
    fah = aci+u32(npdm,aci+0x20)
    assert u64(npdm, fah+4) == 0x200000, 'Only SD filesystem permission should be requested'
    sac, size = aci+u32(npdm,aci+0x28), u32(npdm,aci+0x2C)
    services = set()
    i = sac
    while i < sac+size:
        flags = npdm[i]; length = (flags & 7)+1
        assert not (flags & 0x80), 'No SM service hosting required for a named port'
        services.add(npdm[i+1:i+1+length].decode('ascii')); i += 1+length
    assert services == set(config['service_access']) and '*' not in services
    nso = (OUT/'touchpad.nso').read_bytes()
    assert nso[:4] == b'NSO0'
    startup_bytes = nso_image_size(nso)+u32(npdm,0x1C)
    assert startup_bytes <= 5*1024*1024, 'Backend image plus main stack exceeds the 5 MiB Applet budget'
    pfs = (OUT/'touchpad.nsp').read_bytes()
    assert pfs[:4] == b'PFS0'
    count, strings = u32(pfs,4), u32(pfs,8)
    names_base = 16+24*count; data_base = names_base+strings
    contents = {}
    for n in range(count):
        off, size, nameoff, _ = struct.unpack_from('<QQII',pfs,16+24*n)
        name = pfs[names_base+nameoff:].split(b'\0',1)[0].decode('ascii')
        assert data_base+off+size <= len(pfs)
        contents[name] = pfs[data_base+off:data_base+off+size]
    assert contents == {'main':nso,'main.npdm':npdm}
    probe_cfg = json.loads((ROOT/'probe/probe.json').read_text())
    probe_npdm = (OUT/'touchpad-probe.npdm').read_bytes()
    probe_aci, probe_acid = u32(probe_npdm,0x70),u32(probe_npdm,0x78)
    probe_id = int(probe_cfg['program_id'],16)
    assert probe_npdm[:4] == b'META'
    assert probe_npdm[probe_aci:probe_aci+4] == b'ACI0'
    assert u64(probe_npdm,probe_aci+0x10) == probe_id
    assert u64(probe_npdm,probe_acid+0x210) == probe_id
    assert u64(probe_npdm,probe_acid+0x218) == probe_id
    check_memory_configuration(probe_npdm, expected_pool=2, expected_type=0)
    probe_nso = (OUT/'touchpad-probe.nso').read_bytes()
    assert probe_nso[:4] == b'NSO0'
    probe_static = nso_image_size(probe_nso)
    assert probe_static < 256*1024, 'Legacy System probe must remain small'
    probe_pfs = (OUT/'touchpad-probe.nsp').read_bytes()
    assert probe_pfs[:4] == b'PFS0'
    probe_count, probe_strings = u32(probe_pfs,4),u32(probe_pfs,8)
    assert probe_count == 2
    probe_names_base = 16+24*probe_count
    probe_data_base = probe_names_base+probe_strings
    probe_contents = {}
    for index in range(probe_count):
        off,length,name_off,_ = struct.unpack_from('<QQII',probe_pfs,16+24*index)
        name = probe_pfs[probe_names_base+name_off:].split(b'\0',1)[0].decode('ascii')
        probe_contents[name] = probe_pfs[probe_data_base+off:probe_data_base+off+length]
    assert probe_contents == {'main':probe_nso,'main.npdm':probe_npdm}
    for name in ('touchpad.ovl','touchpad-test.nro','touchpad-diag.nro'):
        nro = (OUT/name).read_bytes()
        assert nro[16:20] == b'NRO0'
        assets = u32(nro,24)
        assert nro[assets:assets+4] == b'ASET'
        nacp_offset, nacp_size = struct.unpack_from('<QQ',nro,assets+24)
        assert nacp_size == 0x4000 and assets+nacp_offset+nacp_size <= len(nro)
        assert nro[assets+nacp_offset:assets+nacp_offset+8] == b'Touchpad'
        for offset in (32,40,48):
            start, length = struct.unpack_from('<II',nro,offset)
            assert start+length <= assets
    print(f'PASS: backend Applet pool + Applet PM group in ACI0/ACID; image + stack = {startup_bytes} bytes ({startup_bytes/1048576:.4f} MiB)')
    print('PASS: AArch64 ELF, NPDM program ID/permissions, legacy System probe, NSO, ExeFS entries, NRO/NACP bounds')

if __name__ == '__main__': check()
