global function TF2VRSC_ContextInit

void function TF2VRSC_ContextInit()
{
#if TF2VR_COCKPIT
    thread TF2VRSC_ContextThink()
#endif
}

#if TF2VR_COCKPIT
void function TF2VRSC_ContextThink()
{
    int heartbeat = 0
    while ( true )
    {
#if UI
        // Phase 3 is a startup transition; phase 5 is normal gameplay.
        bool ready = TF2VR_StartupPhase() == 5 && GetActiveMenu() == null && !uiGlobal.playingVideo
        SetConVarInt( "tf2vrsc_ui_ready", ready ? 1 : 0 )
#endif
#if CLIENT
        entity player = GetLocalViewPlayer()
        bool ready = IsValid( player ) && IsAlive( player ) && !player.IsTitan()
        if ( ready )
            ready = !player.ContextAction_IsActive() && !player.IsWeaponDisabled()
        SetConVarInt( "tf2vrsc_player_ready", ready ? 1 : 0 )
        heartbeat = ( heartbeat + 1 ) % 1000000
        SetConVarInt( "tf2vrsc_heartbeat", heartbeat )
#endif
        WaitFrame()
    }
}
#endif
