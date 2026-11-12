"""Read-only gates for candidate native pickup/putdown images, not acceptance."""
SCENE='054630303172000102'
KEY=(117,496,4294967280)


def readiness(observation,expected,kind,*,carry_observed=False):
    try:
        if kind not in ('pickup','putdown'):
            return False
        frame=observation['frame']
        if type(frame) is not int or frame<0 or observation.get('finished'):
            return False
        rows=[observation['state'],observation['props']]
        for row in rows:
            at=row['preceding_renderer_frame']
            if type(at) is not int or not 0<=frame-at<=360:
                return False
        state,props=[row['value'] for row in rows]
        if (state.get('readable') is not True or state.get('actor_identity_matches') is not True or
                state.get('event_state')!=0 or state.get('loader_state')!=703 or state.get('pause_flags')!=[0,0,0]):
            return False
        if props.get('complete') is not True or props.get('player_instance')!=expected['player_instance']:
            return False
        items=props['items']
        if type(items) is not list or type(props.get('count')) is not int or props['count']!=len(items) or len(items)>128:
            return False
        targets=[p for p in items if (p.get('placed_id'),p.get('profile'),p.get('params'))==KEY and p.get('room') in (0,255)]
        if len(targets)!=1:
            return False
        target=targets[0]
        if (target.get('actor_token'),target.get('instance'))!=(expected['actor_token'],expected['instance']):
            return False
        peers=observation['peers'];desc=peers['descriptor'];cold=desc['cold_import']
        if (peers.get('scene')!=SCENE or type(peers.get('source')) is not int or peers['source']<=0 or
                desc.get('native_source')!=peers['source'] or type(desc.get('captured')) is not int or desc['captured']<1 or
                type(desc.get('retained_settled_carries')) is not int or desc['retained_settled_carries']<1 or
                peers.get('observed_place_sent')!=0 or cold.get('cli_opt_in') is not False or
                cold.get('active') is not False or cold.get('count')!=0):
            return False
        if kind=='pickup':
            return (state.get('held') is True and target.get('held_by_native') is True and
                    target.get('state')==1 and peers.get('source_context')==4)
        return (carry_observed and state.get('held') is False and target.get('held_by_native') is False and
                target.get('state') in (0,3) and peers.get('source_context')==0)
    except (KeyError,TypeError,AttributeError):
        return False
