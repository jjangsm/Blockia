using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

[RequireComponent(typeof(Button))]
public class ButtonActivation : MonoBehaviour
{
    [SerializeField] private List<GameObject> DisableObjs = new();
    [SerializeField] private List<GameObject> EnableObjs = new();

    private Button _button = null;
    private void Awake()
    {
        _button = GetComponent<Button>();
        _button.onClick.AddListener(OnClick);
    }
    private void OnClick()
    {
        foreach(GameObject obj in DisableObjs) obj.SetActive(false);
        foreach(GameObject obj in EnableObjs) obj.SetActive(true);
    }
}
