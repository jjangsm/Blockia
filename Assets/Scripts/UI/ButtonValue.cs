using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

[RequireComponent(typeof(Button))]
public class ButtonValue : MonoBehaviour
{
    [SerializeField] private List<TMP_InputField> InputFields = new();
    [SerializeField] private ValueComponent valueComponent = null;

    private Button _button = null;
    private void Awake()
    {
        _button = GetComponent<Button>();
        _button.onClick.AddListener(OnClick);
    }
    private void OnClick()
    {
        foreach (var field in InputFields) valueComponent.textValues.Enqueue(field.text);
        valueComponent.Activate();
    }
}
