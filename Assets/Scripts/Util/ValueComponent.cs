using System.Collections.Generic;
using UnityEngine;

public abstract class ValueComponent : MonoBehaviour
{
    public Queue<string> textValues = new();
    public abstract void Activate();
}
