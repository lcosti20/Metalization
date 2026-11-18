// A capital ship class: models, toughness, team paint and interior (Data/DA_Ship_*).
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>A ship class: models, toughness, team paint and interior.</summary>
    [CreateAssetMenu(menuName = "Metalization/Capital Ship Data", fileName = "DA_Ship_")]
    public class MetalCapitalShipData : ScriptableObject
    {
        public string displayName = "";
        [Header("Models (prefabs with SOCKET_ children)")]
        [Tooltip("Carries the sockets the other parts are mounted on (engines, shieldgen_L/R, comms, turret_NN, hangar_portal).")]
        public GameObject hullModel;
        public GameObject enginesModel;
        public GameObject shieldGeneratorModel;
        public GameObject commsModel;
        [Tooltip("One per turret_NN socket; its gun socket places turretGunModel.")]
        public GameObject turretBaseModel;
        public GameObject turretGunModel;
        [Header("Turrets")]
        public MetalWeaponData turretWeapon;
        [Tooltip("Turrets engage enemy flyers this close (m).")]
        public float turretRange = 1200f;
        public float turretTurnRate = 70f;
        public float turretAimError = 2.5f;
        [Header("Health")]
        public float hullHealth = 20000f;
        public float shieldGeneratorHealth = 2500f;
        public float enginesHealth = 4000f;
        public float commsHealth = 2000f;
        public float lifeSupportHealth = 3000f;
        [Range(0f, 1f)] public float shieldedHullDamageScale = 0.25f;
        [Header("Visual")]
        public List<MetalTeamColors> teamColors = new List<MetalTeamColors>();
        [Header("Interior")]
        [Tooltip("Scene loaded once per ship at its interior location. Must contain a MetalHangar.")]
        public string interiorScene = "";
        public MetalBotArchetype hangarRobot;
        public float hangarRespawnDelay = 20f;
        [Tooltip("Opening of the hangar mouth (width, height), metres.")]
        public Vector2 hangarMouthSize = new Vector2(80f, 30f);
    }
}
