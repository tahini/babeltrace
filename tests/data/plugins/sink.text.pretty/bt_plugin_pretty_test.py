import bt2


class TheIteratorOfAllEvil(bt2._UserMessageIterator):
    def __init__(self, config, port):
        tc, sc, ec1, params = port.user_data
        trace = tc()
        stream = trace.create_stream(sc)
        event_value = params['value']

        sb_msg = self._create_stream_beginning_message(stream)

        ev_msg1 = self._create_event_message(ec1, stream, 300)
        ev_msg1.event.payload_field["enum_field"] = event_value

        se_msg = self._create_stream_end_message(stream)

        self._msgs = []

        self._msgs.append(sb_msg)

        self._msgs.append(ev_msg1)

        self._msgs.append(se_msg)

        self._at = 0
        config.can_seek_forward = True

    def _user_seek_beginning(self):
        self._at = 0

    def __next__(self):
        if self._at < len(self._msgs):
            msg = self._msgs[self._at]
            self._at += 1
            return msg
        else:
            raise StopIteration


@bt2.plugin_component_class
class TheSourceOfAllEvil(
    bt2._UserSourceComponent, message_iterator_class=TheIteratorOfAllEvil
):
    def __init__(self, config, params, obj):
        tc = self._create_trace_class()

        enum_values_str = params['enum-values']
        
        # Use a clock class with an offset, so we can test with --begin or --end
        # smaller than this offset (in other words, a time that it's not
        # possible to represent with this clock class).
        cc = self._create_clock_class(frequency=1)
        sc = tc.create_stream_class(
            default_clock_class=cc
        )
        
        # Create the enumeration field with the values in parameter
        if params['enum-signed']:
            enumfc = tc.create_signed_enumeration_field_class()
        else:
            enumfc = tc.create_unsigned_enumeration_field_class()
        enum_values = str(enum_values_str).split()
        mappings = {}
        for i in range(0, len(enum_values), 3):
            if params['enum-signed']:
                if not enum_values[i] in mappings.keys():
                    mappings[enum_values[i]] = bt2.SignedIntegerRangeSet([(int(enum_values[i+1]), int(enum_values[i+2]))])
                else:
                    mappings[enum_values[i]].add((int(enum_values[i+1]), int(enum_values[i+2])))
            else:
                if not enum_values[i] in mappings.keys():
                    mappings[enum_values[i]] = bt2.UnsignedIntegerRangeSet([(int(enum_values[i+1]), int(enum_values[i+2]))])
                else:
                    mappings[enum_values[i]].add((int(enum_values[i+1]), int(enum_values[i+2])))
        for x, y in mappings.items():
            enumfc.add_mapping(x, y)

        # Create the struct field to contain the enum field class
        struct_fc = tc.create_structure_field_class()
        struct_fc.append_member('enum_field', enumfc)

        # Create an event class on this stream with the struct field
        ec1 = sc.create_event_class(name='with_enum', payload_field_class=struct_fc)
        self._add_output_port('out', (tc, sc, ec1, params))


bt2.register_plugin(__name__, 'test-pretty')